#!/usr/bin/perl
use common::sense;
use Digest::SHA;

my ($key) = @ARGV;

for my $list (<STDIN>) {
	chomp $list;
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

	my $hex = Digest::SHA::sha256_hex($buffer);
	open my $signature, '|-', "openssl rsautl -sign -inkey '$key' -keyform PEM >$list.sig" or die "Can't run openssl sign";
	print $signature $hex, "\n";
	close $signature;
}
