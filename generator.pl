#!/usr/bin/perl

# Copyright (c) 2013, CZ.NIC
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
use Scalar::Util qw(weaken);
use File::Copy qw(copy);
use Data::Dumper;

# Where to get the packages and their list
my $path = $ARGV[0] or die "Expected the URL of the repository as my first argument\n";

# Download and decompress the list of opkg packages.

# The use of shell here is technically insecure, but the input is ours,
# so it should be OK. Still, it would be nice to do it properly sometime.
my $list_path = "$path/Packages";
open my $descriptions, '<', $list_path or die "Could not open package list in $list_path: $!\n";
my @packages;
{
	# The package descriptions are separated by one empty line, so pretend an empty line is EOF and read all „lines“
	local $/ = "\n\n";
	@packages = <$descriptions>;
}
close $descriptions;

# Parse the packages.

# Drop the newlines at the end.
s/\n\n$// foreach @packages;

# The fields are separated by new lines. However, if the next line starts with whitespace, it is continuation of the previous.
# So, split to the fields. Then, split each field to the name and value and then create a hash from the key-value pairs.
@packages = map +{
	map {
		my @fields = split /:\s*/, $_, 2;
		s/^\s+//gm foreach @fields; # Drop the start-of-line whitespaces
		@fields;
	} split /\n(?!\s)/ }, @packages;

# Index the packages by their name and create data structures for them.
my %packages = map { $_->{Package} => { desc => $_ } } @packages;
# Link the packages by dependencies and reverse dependencies

while (my ($name, $package) = each %packages) {
	my @deps = split /,\s*/, $package->{desc}->{Depends};
	for my $dep (@deps) {
		# FIXME: Some version handling instead of ignoring them (#2704)
		$dep =~ s/\s*\(.*\)\s*//;
		my $dpackage = $packages{$dep} // ( warn "Dependency $dep of $name is missing\n", next );
		$dpackage->{revdep}->{$name} = $package;
		weaken $dpackage->{revdep}->{$name};
		$package->{dep}->{$dep} = $dpackage;
	}
}

# Get list of desired packages from stdin
my @desired = <STDIN>;
chomp @desired;
@desired = grep { not /^\s*#/ } @desired;
my %desired = map { my ($name, $flags) = split /\s+/, $_, 2; ($name, $flags); } @desired;
my @desired_names = map { my ($name) = split /\s+/, $_; $name; } @desired;
my ($order, %desired_order) = (1);

$desired_order{$_} = $order ++ for @desired_names;

sub provide($) {
	my ($package) = @_;
	# Recursion sanity checking & termination
	return if $package->{provided};

	# Parameters
	my $name = $package->{desc}->{Package};
	my $flags = $desired{$name} // '.';
	die "Dependency $name required to be uninstalled\n" if $flags =~ /R/;
	my $version = $package->{desc}->{Version};

	die "Circular dependency in package $name" if $package->{visited};

	# Recursive calls to dependencies
	$package->{visited} = 1;
	&provide($_) foreach values %{$package->{dep}};
	$package->{provided} = 1;

	# The package itself
	my $filename = "$name-$version.ipk";
	copy("$path/$package->{desc}->{Filename}", "packages/$filename") or die "Could not copy $name ($path/$package->{desc}->{Filename}";
	print "$name\t$version\t$flags\n";
	warn "Package $name should be encrypted, but that's not supported yet ‒ you need to encrypt manually\n" if $desired{$name} =~ /E/;
}

sub prio($) {
	my ($name) = @_;
	my ($prio) = ($desired{$name} =~ /(\d+)/);
	$prio //= 1000; # If no prio specified, put it last (1000 being some very large number)
	# Uninstall first
	$prio -= 0.5 if $desired{$name} =~ /R/;
	return $prio;
}

mkdir 'packages';

for my $pname (sort { prio $a <=> prio $b or $desired_order{$a} <=> $desired_order{$b} } @desired_names) {
	if ($desired{$pname} =~ /R/) {
		print "$pname\t-\t$desired{$pname}\n";
	} else {
		provide($packages{$pname} // die "Package $pname doesn't exist\n");
	}
}
