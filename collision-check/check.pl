#!/usr/bin/perl

# Copyright (c) 2015, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#    * Neither the name of the CZ.NIC nor the
#      names of its contributors may be used to endorse or promote products
#      derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL CZ.NIC BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# This script watches an updater repository and detects file collisions between
# packages. The collisions are detected both inside the same build and between
# historical and current build.
#
# Run it after each build and point it to:
# • The base URL of the repository
# • Some install lists (to deduce the list of packages to check)
# • Optionally the definitions of user lists (so all the lists don't need to be
#   listed manually)
#
# It produces lists of files that belong to more than one package. It only
# produces outputs not produced in the previous run (therefore, if file collision
# is not solved, it becomes silent again).
#
# Optionally, it can mark the existence of any file collisions by exit code.

# The script internally heavilly utilizes the AnyEvent library. This allows
# downloading in parallel, while already unpacking the downloaded packages
# in the background, in multiple parallel instances. This, however, results in
# the script being somewhat upside down. First, download of the specified lists
# and definitions is scheduled. Once each of them arrive, it is parsed and
# download of all the packages listed there is scheduled. Again, once each
# package is downloaded, it is enqueued for unpacking. If there are still
# empty slots, an external unpacking script is run, which returns list of
# files contained inside the package. The file names are then stored in
# the data structures.
#
# As each scheduling action takes the callback what should happen once the
# task finishes, the first actions to happen are more to the end of the script.
#
# Each of the downloads produce a conditional variable ‒ something anyevent can
# wait for to finish. After the initial downloading schedule, a cycle waits
# for all the conditional variables to be satisfied. This ensures everything
# will have been downloaded by that time, and an analysis of extracted data may
# proceed. The analysis is done in the usual procedural way.
#
# The history file is perl's Storable serialization of a hash. The hash has two
# elements:
# • „files“ is reference to hash, containing all the known files. Each file then
#   links to another hash, where keys are names of packages. Each package contains
#   yet another hash, which represents set of versions it contained the file in.
# • „reports“, which is set of reported collisions (as exact printed text).

use common::sense;
use utf8;
use Getopt::Long;
use AnyEvent;
use AnyEvent::HTTP;
use AnyEvent::Util qw(run_cmd);
use File::Temp qw(tempdir);
use File::Basename qw(dirname);
use Storable qw(lock_store lock_retrieve);
use Clone qw(clone);

my ($history_file, $verbose, $base_url, @lists, $initial, $store, $report_all, $fail, @definitions);

GetOptions
	verbose => \$verbose,		# Be verbose and produce lot of debug info about what is being done

	'history=s' => \$history_file,	# File to store history between runs
	'store=s' => \$store,		# Store snapshot of current files under given name

	initial => \$initial,		# The history file doesn't exist yet, this is the first run
	'url=s' => \$base_url,		# The basic URL of the repository (should contain lists/ and packages/ subdirectories)
	'list=s' => \@lists,		# Package lists, may be provided multiple times
	'definitions=s' => \@definitions,# The package list definitions, to produce more lists


	'report-all' => \$report_all,	# Report all found collisions, even the ones reported in previous run
	fail => \$fail			# Fail with exit code 2 if anything is reported

or die "Bad params\n";

die "No history file specified, use --history\n" unless $history_file;
die "No base URL specified, use --url\n" unless $base_url;
die "No package lists specified, use --list (possibly multiple times)\n" unless @lists;

sub dbg(@) {
	print STDERR "DBG: ", @_ if $verbose;
}

my $history;
my $files;
if (!$initial) {
	dbg "Reading history $history_file\n";
	$history = lock_retrieve $history_file;
	die "Couldn't read history file $history_file: $!\n" unless $history;
	$files = clone $history->{files};
	if ($store) {
		for my $file_owners (values %{$history->{files}}) {
			for my $package (values %$file_owners) {
				delete $package->{$store};
			}
		}
	}
}

my $err = 0;

my %packages;
my @condvars;
my $unpack_cmd = dirname($0) . "/pkg-unpack";
dbg "Using $unpack_cmd as the unpacking command\n";
my $unpack_limit = 8; # How many unpacks may run in the background
my @unpack_queue;

my $workdir = tempdir(CLEANUP => 1);
dbg "Using $workdir as working directory\n";

# Try to run another unpack, provided there's package to unpack and
# we have an empty slot.
sub check_unpack_queue() {
	unless (@unpack_queue) {
		dbg "Nothing in the unpack queue\n";
		return;
	}
	unless ($unpack_limit) {
		dbg "No free unpack slots\n";
		return;
	}
	$unpack_limit --;
	my $params = shift @unpack_queue;
	&handle_pkg(@$params);
}

# Package arrived.
sub handle_pkg($$$) {
	my ($name, $body, $cv) = @_;
	my $output;
	dbg "Unpacking $name\n";
	my $finished = run_cmd [$unpack_cmd, "$workdir/$name"],
		'>' => \$output,
		'<' => \$body,
		close_all => 1;
	$finished->cb(sub {
		my $ecode = shift->recv;
		if ($ecode) {
			warn "Failed to unpack $name: $ecode\n";
			$err = 1;
		} else {
			dbg "Unpacked $name\n";
			for my $f (split /\0/, $output) {
				next unless $f; # Skip ghost empty file at the end
				$files->{$f}->{$name}->{current} = 1;
				$history->{files}->{$f}->{$name}->{$store} = 1 if $store;
			}
			$packages{$name}->{unparsed} = $output;
		}
		$cv->send;
		$unpack_limit ++;
		check_unpack_queue();
	});
}

# Schedule a package to download.
sub get_pkg($) {
	my ($name) = @_;
	my $url = "$base_url/packages/$packages{$name}->{file}";
	dbg "Downloading package $url\n";
	my $cv = AnyEvent->condvar;
	push @condvars, $cv;
	http_get $url, tls_ctx => "high", sub {
		my ($body, $hdrs) = @_;
		if (defined $body and $hdrs->{Status} == 200) {
			dbg "Downloaded package $name, going to unpack\n";
			push @unpack_queue, [$name, $body, $cv];
			check_unpack_queue;
		} else {
			warn "Failed to download $name: $hdrs->{Status} $hdrs->{Reason}\n";
			$err = 1;
			$cv->send;
		}
	};
}

# A list arrived, parse it.
sub handle_list($) {
	my ($list) = @_;
	open my $input, '<:utf8', \$list or die "Error reading list: $!\n";
	while (<$input>) {
		chomp;
		my ($name, $version, $flags, $hash) = split;
		# We don't consider packages that are to be removed
		next if $flags =~ /R/;
		if (exists $packages{$name}) {
			dbg "Package $name already present, skipping\n";
			next;
		}
		$packages{$name} = {
			file => "$name-$version.ipk",
			hash => $hash
		};
		get_pkg $name;
	}
}

# Schedule downloading a list
sub get_list($) {
	my ($name) = @_;
	my $url = "$base_url/lists/$name";
	dbg "Downloading list $name from '$url'\n";
	my $cv = AnyEvent->condvar;
	push @condvars, $cv;
	http_get $url, tls_ctx => "high", sub {
		my ($body, $hdrs) = @_;
		if (defined $body and $hdrs->{Status} == 200) {
			dbg "Downloaded list $name\n";
			handle_list($body);
		} else {
			warn "Failed to download $name: $hdrs->{Status} $hdrs->{Reason}\n";
			$err = 1;
		}
		$cv->send;
	};
}

# A definition of lists arrived. Parse it and schedule the lists to download.
sub handle_definition($$) {
	my ($def, $suffix) = @_;
	for my $line (split /\n/, $def) {
		if ($line =~ /\['(.*)'\] = \{/) {
			dbg "Found reference to list $1\n";
			get_list "$1$suffix";
		}
	}
}

# Schedule download of a definition.
sub get_definition($) {
	my ($name) = @_;
	my ($suffix) = ($name =~ /(-.*)/);
	my $url = "$base_url/lists/$name";
	dbg "Downloading definition $name from '$url'\n";
	my $cv = AnyEvent->condvar;
	http_get $url, tls_ctx => "high", sub {
		my ($body, $hdrs) = @_;
		if (defined $body and $hdrs->{Status} == 200) {
			dbg "Downloaded definition $name\n";
			handle_definition($body, $suffix);
		} else {
			warn "Failed to download definition $name: $hdrs->{Status} $hdrs->{Reason}\n";
			$err = 1;
		}
		$cv->send;
	};
}

dbg "Going to download lists\n";
get_definition $_ for @definitions;
get_list $_ for @lists;

# Wait for all background tasks to finish.
dbg "Waiting for downloads and unpacks to finish\n";
while (@condvars) {
	my $cv = shift @condvars;
	$cv->recv;
}

dbg "Waiting done\n";

my $reported;

# Go through all the files and look for the ones that are in more than one package.
for my $f (sort keys %$files) {
	my @packages = keys %{$files->{$f}};
	if (@packages != 1) {
		my $report = "Collision on file '$f':\n";
		for my $package (sort @packages) {
			$report .= "• $package (" . (join ', ', sort keys %{$files->{$f}->{$package}}) . ")\n";
		}
		$reported->{$report} = 1;
		if ($report_all or not exists $history->{reported}->{$report}) {
			print $report;
			$err = 2 if $fail;
		}
	}
}

$history->{reported} = $reported;

dbg "Writing history $history_file\n";
if (not defined lock_store $history, $history_file) {
	die "Couldn't store history to $history_file: $!\n";
}

exit $err;
