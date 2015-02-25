#!/usr/bin/perl -T

# Copyright (c) 2014, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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

# This is to be placed to the base directory of api.turris.cz. It receives request
# for multiple lists from client, checks if they changed since, and packs new
# versions.

use common::sense;
use File::Temp qw(tempdir);
use File::Copy qw(copy);
use Digest::MD5;

sub error($$) {
	my ($code, $text) = @_;
	print "Status: $code\n\n";
	print STDERR $text;
	exit;
}

# Move to the place where lists live
my $serie = <>;
chomp $serie;
$serie =~ s/^(.)(.{8})$/$1\/$2/;
my ($s) = $serie =~ /^((\d+\/)?[a-f0-9]{8}|unknown-revision)$/i or error "404 Not Found", "Bad serie '$serie'\n";
$serie = $s;
chdir "updater-repo/$serie/lists" or die "Couldn't set directory '$serie/lists': $!\n";
$serie =~ s#.*/##;

# Who is asking for data? It'll influence choice of the files
my $id = <>;
chomp $id;
my ($i) = $id =~ /^([a-z0-9z]+)$/i or error "404 Not Found", "Bad ID '$id'\n";
$id = $i;

my $dir = tempdir CLEANUP => 1;

# Choose the right file or undef if it doesn't exist
sub candidate($) {
	my ($name) = @_;
	my $specific = "$name-$serie$id";
	my $generic = "$name-generic";
	# Correction for the base „nameless“ file
	$specific =~ s/^base-//;
	$generic =~ s/^base-//;
	if (-f $specific) {
		return $specific;
	} elsif (-f $generic) {
		return $generic;
	} else {
		return undef;
	}
}

open my $sfile, '>', "$dir/status" or die "Could not write status file\n";

# Go through the requests for files and check if they are the same as the device has
my @files;
while (<>) {
	chomp;
	my ($name, $hash) = /^([-_a-z0-9]+)\s+([a-f0-9]{32}|-)$/ or die "418 I'm a very confused teapot ☹", "Bad request line '$_'\n";
	my $candidate = candidate $name;
	if (defined $candidate) {
		my $md5ctx = Digest::MD5->new;
		open my $list, '<', $candidate or die "Couldn't read file '$candidate': $!\n";
		$md5ctx->addfile($list);
		close $list;
		my $md5 = $md5ctx->hexdigest;
		if (lc $md5 eq lc $hash) {
			print $sfile "$name UNCHANGED\n";
		} else {
			copy $candidate, "$dir/$name" or die "Couldn't copy list file '$candidate': $!\n";
			copy "$candidate.sig", "$dir/$name.sig" or die "Couldn't copy sig file '$candidate.sig': $!\n";
			push @files, $name, "$name.sig";
			print $sfile "$name PACKED\n";
		}
	} else {
		print $sfile "$name MISSING\n";
	}
}
close $sfile;

# Pack the output
chdir $dir or die "Couldn't chdir to the temporary directory '$dir': $!\n";
print "Content-Type: application/octet-stream\n";
print "Status: 200 OK\n";
print "\n";
$ENV{PATH} = '/bin/:/usr/bin/';
system '/bin/tar', 'cj', 'status', @files and die "Failed to run tar\n";

chdir '/'; # Escape from the directory, so perl can delete it
