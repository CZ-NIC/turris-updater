#!/usr/bin/env perl
#
# xxdi.pl - perl implementation of 'xxd -i' mode
#
# Copyright 2013 Greg Kroah-Hartman <gregkh@linuxfoundation.org>
# Copyright 2013 Linux Foundation
#
# Released under the GPLv2.
#
# Implements the "basic" functionality of 'xxd -i' in perl to keep build
# systems from having to build/install/rely on vim-core, which not all
# distros want to do.  But everyone has perl, so use it instead.
#

use strict;
use warnings;

die "Usage: xddi.pl VARIABLE_NAME INPUT OUTPUT" unless @ARGV == 3;

my $var_name = $ARGV[0];
my $indata = do {
    local $/;
    open my $f, "<", $ARGV[1] or die "Could not open input $ARGV[1]: $!\n";
    <$f>;
};
my $len_data = length($indata);
my $num_digits_per_line = 12;

open(*STDOUT, '>', $ARGV[2]) or die "Couldn't open output $ARGV[2]: $!\n";
binmode STDOUT;

print <<END;
// This file is generated. Do not edit!
#include <stdint.h>


static const size_t $var_name\_len = $len_data;

END

print "static const uint8_t $var_name\[] = {";
for (my $key= 0; $key < $len_data; $key++) {
	print "\n\t" if ($key % $num_digits_per_line == 0);
	printf("0x%.2x, ", ord(substr($indata, $key, 1)));
}
print "\n};";
