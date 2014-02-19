#!/usr/bin/perl
use common::sense;

# Go through packages and check it is somewhat sane. There are forbidden packages for some branches, for example.

my %forbid = (
	master => [qw(getbranch-test)],
	deploy => [qw(getbranch-test)],
);

my $list;

while (<>) {
	chomp;
	if (/^list\s+(\S+)/) {
		$list = $1;
	} elsif (my ($name) = /^repo\s+(\S+)/) {
		open my $input, '<', $list or die "Could not open $list: $!\n";
		my @input = <$input>;
		close $input;
		chomp @input;
		my %forbidden = map { $_ => 1 } @{$forbid{$name}};
		for my $pkg (@input) {
			my ($pkgname, $flags) = split /\s+/, $pkg, 1;
			next if $flags =~ /R/;
			die "Forbidden $pkgname on $name\n" if $forbidden{$pkgname};
		}
	}
}
