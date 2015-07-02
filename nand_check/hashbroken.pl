#!/usr/bin/perl

# Copyright (c) 2015 CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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
