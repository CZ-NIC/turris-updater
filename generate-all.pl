#!/usr/bin/perl
use common::sense;
use File::Path;
use Cwd 'abs_path';

my $indir;

my $reponame;

my ($generator, $fixer, $list_dir, $key) = map { abs_path $_ } @ARGV[0..3];

my $list;

my @lists;

sub leave() {
	return unless $indir;
	open my $fixer_p, '|-', $fixer, $key or die "Couldn't start fixer: $!";
	print $fixer_p "$_\n" for @lists;
	close $fixer_p or die "Fixer failed: $!";
	@lists = ();
	chdir '..' or die "Couldn't go up: $!";
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
		die "No list specified yet" unless $list;
		if (system("'$generator' '$2' <'$list_dir/$list' >'lists/$1'")) {
			die "Failed to run generator";
		}
		push @lists, "lists/$1";
	} elsif (/^alias\s+(.*?)\s*$/) {
		symlink "$reponame", "lists/$1" or die "Couldn't create alias: $!";
		symlink "$reponame.sig", "lists/$1.sig" or die "Couldn't create sig alias: $!";
	} elsif (/^list\s+(.*?)\s*$/) {
		$list = $1;
	} else {
		die "Unknown command: $_";
	}
}
leave;
