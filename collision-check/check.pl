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
use File::Temp qw(tempdir);
use Data::Dumper;

my ($history_file, $debug, $base_url, @lists);

GetOptions
	'history=s' => \$history_file,
	debug => \$debug,
	'url=s' => \$base_url,
	'list=s' => \@lists
or die "Bad params\n";

die "No history file specified, use --history\n" unless $history_file;
die "No base URL specified, use --url\n" unless $base_url;
die "No package lists specified, use --list (possibly multiple times)\n" unless @lists;

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

sub handle_pkg($$) {

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
			dbg "Downloaded package $name\n";
			handle_pkg($name, $body);
		} else {
			warn "Failed to download $name: $hdrs->{Status} $hdrs->{Reason}\n";
			$err = 1;
		}
		$cv->send;
	};
}

my $workdir = tempdir(CLEANUP => 1);
dbg "Using $workdir as working directory\n";

for my $list (@list_contents) {
	open my $input, '<:utf8', \$list or die "Error reading list: $!\n";
	while (<$input>) {
		chomp;
		my ($name, $version, $flags, $hash) = split;
		# We don't consider packages that are to be removed
		next if $flags =~ /R/;
		$packages{$name} = {
			file => "$name-$version.ipk",
			hash => $hash
		};
		get_pkg $name;
	}
}

$_->recv for @condvars;
@condvars = ();
