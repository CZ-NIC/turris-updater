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
use Digest::SHA;

my ($key) = @ARGV;

for my $list (<STDIN>) {
	chomp $list;
	my $hex;
	print STDERR $list, "\n";
	if ($list ne 'definitions') {
		open my $list_file, '<', $list or die "Could not read $list: $!\n";
		my @packages = map { chomp; [split /\t/, $_] } (<$list_file>);
		close $list_file;

		my $buffer;
		open my $output, '>', \$buffer;
		for my $package (@packages) {
			local $\ = "\n";
			local $, = "\t";
			my ($name, $version, $flags) = @$package;
			if ($flags =~ /[RE]/) {
				print $output $name, $version, $flags;
			} else {
				my $hash = Digest::SHA->new(256);
				$hash->addfile("packages/$name-$version.ipk");
				my $hash_result = $hash->hexdigest;
				print $output $name, $version, $flags, $hash_result;
			}
		}
		close $output;

		open my $list_file, '>', $list or die "Could not write $list: $!\n";
		print $list_file $buffer;
		close $list_file;

		$hex = Digest::SHA::sha256_hex($buffer);
	} else {
		my $hash = Digest::SHA->new(256);
		$hash->addfile($list);
		$hex = $hash->hexdigest;
	}
	open my $signature, '|-', "openssl rsautl -sign -inkey '$key' -keyform PEM >$list.sig" or die "Can't run openssl sign";
	print $signature $hex, "\n";
	close $signature;
}
