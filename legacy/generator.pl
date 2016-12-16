#!/usr/bin/perl

# Copyright (c) 2013-2015, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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
use Getopt::Long;

# Where to get the packages and their list
my ($path, $list_dir, $output_dir, @omit, $list_defs_file);

GetOptions
	'path=s' => \$path,
	'list-dir=s' => \$list_dir,
	'output-dir=s' => \$output_dir,
	'omit=s' => \@omit,
	'list-defs=s' => \$list_defs_file,
or die "Bad params";

my %omit = map { $_ => 1 } @omit;

my %list_defs;

if ($list_defs_file) {
	open my $list_defs, '<:utf8', $list_defs_file or die "Couldn't read list definitions file $list_defs_file: $!\n";
	local $/ = ""; # Split by paragraphs
	while (my $def = <$list_defs>) {
		chomp $def;
		my @split = split /\n/, $def;
		die "Wrong number of lines for list def (" . (scalar @split) . "): $def\n" unless @split == 5;
		my ($id, $title_cs, $title_en, $desc_cs, $desc_en) = @split;
		$list_defs{$id} = {
			title_cs => $title_cs,
			title_en => $title_en,
			description_cs => $desc_cs,
			description_en => $desc_en
		};
	}
} # Close $list_defs by going out of scope

my @packages;

sub read_packages($) {
	my ($subdir) = @_;
	my $list_path = "$path/$subdir/Packages";
	open my $descriptions, '<', $list_path or return undef;
	my @packages_local;
	{
		# The package descriptions are separated by one empty line, so pretend an empty line is EOF and read all „lines“
		local $/ = "\n\n";
		@packages_local = <$descriptions>;
	}
	close $descriptions;
	push @packages, map { { data => $_, subdir => $subdir } } @packages_local;
	return 1;
}

# Parse the packages.
if (not read_packages undef) {
	my @subdirs = <$path/*>;
	for my $sd (@subdirs) {
		if (-d $sd) {
			$sd =~ s#/$##;
			$sd =~ s#.*/##;
			read_packages $sd or die "Couldn't read $path/$sd/Packages: $!\n";
		}
	}
}

# Drop the newlines at the end.
$_->{data} =~ s/\n\n$// foreach @packages;

# The fields are separated by new lines. However, if the next line starts with whitespace, it is continuation of the previous.
# So, split to the fields. Then, split each field to the name and value and then create a hash from the key-value pairs.
@packages = map +{
	src_dir => $_->{subdir},
	map {
		my @fields = split /:\s*/, $_, 2;
		s/^\s+//gm foreach @fields; # Drop the start-of-line whitespaces
		@fields;
	} split /\n(?!\s)/, $_->{data} }, @packages;

# Index the packages by their name and create data structures for them.
my %packages = map { $_->{Package} => { desc => $_ } } @packages;
# Link the packages by dependencies and reverse dependencies

while (my ($name, $package) = each %packages) {
	my @deps = split /,\s*/, $package->{desc}->{Depends};
	for my $dep (@deps) {
		# FIXME: Some version handling instead of ignoring them (#2704)
		$dep =~ s/\s*\(.*\)\s*//;
		my $dpackage = $packages{$dep};
		unless($dpackage) {
			warn "Dependency $dep of $name is missing\n";
			next;
		}
		$dpackage->{revdep}->{$name} = $package;
		weaken $dpackage->{revdep}->{$name};
		$package->{dep}->{$dep} = $dpackage;
	}
}

# Get list of desired packages from stdin
my @desired = <STDIN>;
chomp @desired;
@desired = grep { not /^\s*#/ } @desired;
my $pnum = 1;
my (%desired, @desired_names);
for my $line (@desired) {
	my ($name, $flags) = split /\s+/, $line, 2;
	$name = "__PASSTHROUGH__" . ($pnum ++) . "__$name" if $flags =~ /P/;
	$desired{$name} = $flags;
	push @desired_names, $name;
}
my ($order, %desired_order) = (1);

$desired_order{$_} = $order ++ for @desired_names;
my @output;

sub provide($;$$) {
	my $package = shift;
	my ($name, $flags) = (@_, $package->{desc}->{Package}, $desired{$package->{desc}->{Package}});
	$flags //= '.';
	# Recursion sanity checking & termination
	return if $package->{provided} and $flags !~ /P/;

	# Parameters
	die "Dependency $name required to be uninstalled\n" if $flags =~ /R/;
	my $version = $package->{desc}->{Version};

	die "Circular dependency in package $name" if $package->{visited};

	if ($flags !~ /P/) { # Passthrough → Don't worry about deps and don't create the package
		# Recursive calls to dependencies
		$package->{visited} = 1;
		&provide($_) foreach values %{$package->{dep}};
		$package->{provided} = 1;

		my $filename = "$name-$version.ipk";
		copy("$path/$package->{desc}->{src_dir}/$package->{desc}->{Filename}", "packages/$filename") or die "Could not copy $name ($path/$package->{desc}->{src_dir}/$package->{desc}->{Filename}";
		push @output, $name unless $desired{$name} =~ /I/;
	}
	return if $omit{$name};
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

my @lists;

for my $pname (sort { prio $a <=> prio $b or $desired_order{$a} <=> $desired_order{$b} } @desired_names) {
	my $flags = $desired{$pname};
	my $name = $pname;
	$name =~ s/^__PASSTHROUGH__\d+__//;
	if ($flags =~ /R/) {
		print "$name\t-\t$flags\n";
	} elsif ($flags =~ /X/) {
		# It is not a name, but a regular expression. Use all matching ones.
		my @matched;
		for my $available (keys %packages) {
			next unless $available =~ /$name/;
			$desired{$available} = $flags;
			push @matched, $available;
		}
		# Go through the packages after the flags are set for them, so the dependencies don't bring in something sooner without the flags.
		for my $matched (@matched) {
			provide $packages{$matched};
		}
	} elsif ($flags =~ /L/) {
		push @lists, $name;
	} else {
		die "Package $name doesn't exist\n" unless exists $packages{$name};
		provide $packages{$name}, $name, $flags;
	}
}

my $omits = join ' ', map "'--omit' '$_'", @output;

for my $list (@lists) {
	my ($list_nodot) = ($list =~ /^([^.]+)/);
	if (system("'$0' '--path' '$path' $omits <'$list_dir$list' >'$output_dir/$list_nodot'")) {
		die "Failed to run sub-generator for $list\n";
	}
}

# Escape a string so it can be fed to lua
sub lua_escape($) {
	my ($text) = @_;
	$text =~ s/'/\\'/g;
	return $text;
}

if (@lists) {
	open my $list_file, '>:utf8', "$output_dir/definitions" or die "Couldn't write definitions: $!\n";
	print $list_file "lists = {\n", (join ",\n", map {
		my ($name) = /^([^.]+)/;
		if (exists $list_defs{$name}) {
			"['$name'] = {\n" . (join ",\n", map { "    $_ = '" . lua_escape($list_defs{$name}->{$_}) . "'" } sort keys %{$list_defs{$name}}) . "\n}"
		} else {
			();
		}
	} sort @lists), "\n};";
	close $list_file;
}
