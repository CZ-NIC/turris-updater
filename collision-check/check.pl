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

my ($history_file, $debug, $base_url, @lists, $initial, $store);

GetOptions
	'history=s' => \$history_file,
	debug => \$debug,
	'url=s' => \$base_url,
	'list=s' => \@lists,
	initial => \$initial,
	'store=s' => \$store
or die "Bad params\n";

die "No history file specified, use --history\n" unless $history_file;
die "No base URL specified, use --url\n" unless $base_url;
die "No package lists specified, use --list (possibly multiple times)\n" unless @lists;

my $history;
my $files;
if (!$initial) {
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

sub dbg(@) {
	print STDERR "DBG: ", @_ if $debug;
}

my $err;

my @list_contents;
{
	dbg "Going to download lists\n";
	my @condvars;

	for my $l (@lists) {
		my $url = "$base_url/lists/$l";
		dbg "Downloading list $l from '$url'\n";
		my $cv = AnyEvent->condvar;
		push @condvars, $cv;
		http_get $url, tls_ctx => "high", sub {
			my ($body, $hdrs) = @_;
			if ($body) {
				dbg "Downloaded list $l\n";
				push @list_contents, $body;
			} else {
				warn "Failed to download $l: $hdrs->{Status} $hdrs->{Reason}\n";
				$err = 1;
			}
			$cv->send;
		};
	}
	dbg "Waiting for downloads to finish\n";
	$_->recv for @condvars;
	dbg "Downloads done\n";
}

my %packages;
my @condvars;
my $unpack_cmd = dirname($0) . "/pkg-unpack";
dbg "Using $unpack_cmd as the unpacking command\n";
my $unpack_limit = 8; # How many unpacks may run in the background
my @unpack_queue;

my $workdir = tempdir(CLEANUP => 1);
dbg "Using $workdir as working directory\n";

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

sub get_pkg($) {
	my ($name) = @_;
	my $url = "$base_url/packages/$packages{$name}->{file}";
	dbg "Downloading package $url\n";
	my $cv = AnyEvent->condvar;
	push @condvars, $cv;
	http_get $url, tls_ctx => "high", sub {
		my ($body, $hdrs) = @_;
		if ($body) {
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

for my $list (@list_contents) {
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

while (@condvars) {
	my $cv = shift @condvars;
	$cv->recv;
}

for my $f (sort keys %$files) {
	if (keys %{$files->{$f}} != 1) {
		print "Local file collision on file '$f':\n";
		print map "• $_\n", sort keys %{$files->{$f}};
	}
}

if (not defined lock_store $history, $history_file) {
	die "Couldn't store history to $history_file: $!\n";
}
