#!/usr/bin/perl
use common::sense;
use File::Path;
use Cwd 'abs_path';

my $indir;

my $reponame;

my ($generator, $list, $key) = map { abs_path $_ } @ARGV[0..2];

while (<STDIN>) {
	chomp;
	s/#.*//;
	next unless /\S/;
	if (/^dir\s+(.*?)\s*$/) {
		chdir '..' if $indir;
		mkdir $1 or die "Can't create $1: $!";
		chdir $1 or die "Can't enter $1: $!";
		$indir = 1;
		mkdir 'lists' or die "Couldn't create lists: $!";
	} elsif (/^repo\s+(\w+)\s+(.*?)\s*$/) {
		$reponame = $1;
		if (system("'$generator' '$2' '$key' 'lists/$1.sig' <'$list' >'lists/$1'")) {
			die "Failed to run generator";
		}
	} elsif (/^alias\s+(.*?)\s*$/) {
		symlink "$reponame", "lists/$1" or die "Couldn't create alias: $!";
		symlink "$reponame.sig", "lists/$1.sig" or die "Couldn't create sig alias: $!";
	} else {
		die "Unknown command: $_";
	}
}
