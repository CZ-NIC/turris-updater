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
use JSON qw(decode_json);
use Storable qw(freeze);

my %output;

# The actual merging
for my $in (@ARGV) {
	open my $file, '<', $in or die "Could not read input file $in: \n";
	local $/;
	my $packages = decode_json <$file>;
	close $file;
	while (my ($pname, $files) = each %$packages) {
		push @{$output{$pname}}, $files;
	}
}

# Deduplicate
my %dedup;
sub do_dedup($) {
	my ($values) = @_;
	my @result;
	my %seen;
	local $Storable::canonical = 1;
	for my $val (@$values) {
		my $can = freeze $val;
		unless ($seen{$can}) {
			$seen{$can} = 1;
			push @result, $val;
		}
	}
	return \@result;
}
@dedup{keys %output} = map do_dedup $_, values %output;

print JSON->new->allow_nonref->pretty->encode(\%dedup);
