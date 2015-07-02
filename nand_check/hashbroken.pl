#!/usr/bin/perl
use common::sense;
use utf8;
use Data::Dumper;

# Analyzer of the logs, it produces report about which files needed to be reinstalled where.
# Just pipe list of log files to its STDIN.

print "====== Starting report =====\n";

my %broken;

while (my $fname = <>) {
	chomp $fname;
	open my $input, '<:utf8', $fname or die "Couldn't open $fname: $!\n";
	while (<$input>) {
		if (my ($file, $package, $hash) = /updater-hash-check\[\]: Hash for file (\S+) of (\S+) does not match, got (\S+),/) {
			my $client = $fname;
			$client =~ s#.*/##;
			$client =~ s#\.log##;
			$broken{$package}->{$file}->{$hash}->{$client} ++;
		}
	}
}

for my $package (sort keys %broken) {
	my $pval = $broken{$package};
	print $package, "\n", '=' x length $package, "\n";
	for my $file (sort keys %$pval) {
		my $fval = $pval->{$file};
		print " • $file\n";
		for my $hash (sort keys %$fval) {
			my $hval = $fval->{$hash};
			print "  ◦ $hash: ";
			print join ", ", map { "$_($hval->{$_})" } sort keys %$hval;
			print "\n";
		}
	}
}

print "======= Finishing report ======\n";
