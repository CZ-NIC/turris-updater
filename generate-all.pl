#!/usr/bin/perl

# Copyright (c) 2013, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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
use File::Path;
use File::Temp;
use Cwd 'abs_path';

my $indir;

my $reponame;

my ($generator, $fixer, $list_dir, $key) = map { abs_path $_ } @ARGV[0..3];

my $list;

my @lists;

my %categories;

sub leave() {
	return unless $indir;
	open my $fixer_p, '|-', $fixer, $key or die "Couldn't start fixer: $!";
	print $fixer_p "$_\n" for @lists;
	close $fixer_p or die "Fixer failed: $!";
	@lists = ();
	chdir '..' or die "Couldn't go up: $!";
}

sub alias_user($) {
	my ($name) = @_;
	for my $list (<lists/$reponame.user/*>) {
		my ($filename) = ($list =~ /.*\/(.*?)$/);
		$list =~ s/^lists\///;
		symlink "$list", "lists/$filename-$name";
		symlink "$list.sig", "lists/$filename-$name.sig";
	}
}

sub alias($) {
	my ($name) = @_;
	symlink "$reponame", "lists/$name" or die "Couldn't create alias: $!";
	symlink "$reponame.sig", "lists/$name.sig" or die "Couldn't create sig alias: $!";
	alias_user $name;
}

while (<STDIN>) {
	chomp;
	s/#.*//;
	next unless /\S/;
	s/\$HOME/$ENV{HOME}/g;
	if (/^dir\s+(.*?)\s*$/) {
		leave;
		mkdir $1 or die "Can't create $1: $!";
		chdir $1 or die "Can't enter $1: $!";
		$indir = 1;
		mkdir 'lists' or die "Couldn't create lists: $!";
	} elsif (/^repo\s+(\w+)\s+(.*?)\s*$/) {
		$reponame = $1;
		my $path = $2;
		die "No list specified yet" unless $list;
		print "Running generator on $path for $reponame\n";
		mkdir "lists/$reponame.user";
		my $input = "$list_dir/$list";
		my @delete;
		if (-e "$path/root/usr/lib/opkg/status") {
			my ($fh, $fn) = File::Temp->new(UNLINK => 0);
			open my $pkglist, '<', $input or die "Could not open input $input: $!\n";
			print $fh $_ while (<$pkglist>);
			close $pkglist;
			open my $pkglist, '<', "$path/root/usr/lib/opkg/status" or die "Could not open list of base packages in the image: $!";
			while (<$pkglist>) {
				chomp;
				if (/^Package: (.*)/) {
					print $fh "$_	100\n";
				}
			}
			close $pkglist;
			close $fh;
			$input = $fn;
			push @delete, $fn;
		}
		if (system("'$generator' '--path' '$path/packages' --list-dir '$list_dir/' '--output-dir' 'lists/$reponame.user' <'$list_dir/$list' >'lists/$reponame'")) {
			die "Failed to run generator";
		}
		unlink @delete;
		push @lists, "lists/$reponame", <lists/$reponame.user/*>;
		alias_user $reponame;
	} elsif (/^alias\s+(.*?)\s*$/) {
		alias $1;
	} elsif (/^branch\s+(.*?)\s*$/) {
		alias $_ for @{$categories{$1}};
	} elsif (/^list\s+(.*?)\s*$/) {
		$list = $1;
	} elsif (/^categories\s+(.*?)\s*$/) {
		open my $category_file, '<', $1 or die "Couldn't read category list $1: $!\n";
		my $category;
		while (<$category_file>) {
			chomp;
			s/#.*//;
			next unless /\S/;
			if (/^\s*(\S+)\s*:$/) {
				$category = $1;
			} else {
				/(\S+)/;
				push @{$categories{$category}}, $1;
			}
		}
		close $category_file;
	} else {
		die "Unknown command: $_";
	}
}
leave;
