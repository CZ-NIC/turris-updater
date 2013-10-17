#!/usr/bin/perl
use common::sense;
use utf8;
use Scalar::Util qw(weaken);
use Data::Dumper;
use Digest::SHA;

my $output_buffer;
open my $output, '>', \$output_buffer or die "Could not redirect output: $!\n";

# Where to get the packages and their list
my $url = $ARGV[0] or die "Expected the URL of the repository as my first argument\n";
my $key = $ARGV[1] or die "I want an RSA key for generating a signature\n";
my $sigfilename = $ARGV[2] or die "I want to know where to store signature\n";

# Download and decompress the list of opkg packages.

# The use of shell here is technically insecure, but the input is ours,
# so it should be OK. Still, it would be nice to do it properly sometime.
my $list_url = "$url/Packages.gz";
open my $descriptions, '-|', "wget '$list_url' -O - | gzip -d" or die "Could not start download of $list_url $!\n";
my @packages;
{
	# The package descriptions are separated by one empty line, so pretend an empty line is EOF and read all „lines“
	local $/ = "\n\n";
	@packages = <$descriptions>;
}
close $descriptions or die "Failed to download $list_url\n";

# Parse the packages.

# Drop the newlines at the end.
s/\n\n$// foreach @packages;

# The fields are separated by new lines. However, if the next line starts with whitespace, it is continuation of the previous.
# So, split to the fields. Then, split each field to the name and value and then create a hash from the key-value pairs.
@packages = map +{
	map {
		my @fields = split /:\s*/, $_, 2;
		s/^\s+//gm foreach @fields; # Drop the start-of-line whitespaces
		@fields;
	} split /\n(?!\s)/ }, @packages;

# Index the packages by their name and create data structures for them.
my %packages = map { $_->{Package} => { desc => $_ } } @packages;
# Link the packages by dependencies and reverse dependencies

while (my ($name, $package) = each %packages) {
	my @deps = split /,\s*/, $package->{desc}->{Depends};
	for my $dep (@deps) {
		# FIXME: Some version handling instead of ignoring them (#2704)
		$dep =~ s/\s*\(.*\)\s*//;
		my $dpackage = $packages{$dep} // ( warn "Dependency $dep of $name is missing\n", next );
		$dpackage->{revdep}->{$name} = $package;
		weaken $dpackage->{revdep}->{$name};
		$package->{dep}->{$dep} = $dpackage;
	}
}

# Get list of desired packages from stdin
my @desired = <STDIN>;
chomp @desired;
my %desired = map { my ($name, $flags) = split /\s+/, $_, 2; ($name, $flags); } @desired;

sub mark($) {
	my ($package) = @_;
	return if $package->{desired};
	$package->{desired} = 1;
	&mark($_) foreach values %{$package->{dep}};
}

# Mark each packet as desired, and its deps recursively too. Skip the ones to remove.
for my $desired (keys %desired) {
	next if $desired{$desired} =~ /R/;
	mark($packages{$desired} // die "Package $desired doesn't exist\n");
}

# Generate list of packages to be installed
my %final = map { $_->{desc}->{Package} => $_ } grep $_->{desired}, values %packages;

# Print out the packages to remove first
print $output map "$_\t-\t$desired{$_}\n", (grep $desired{$_} =~ /R1/, keys %desired);

# Keep picking the things without dependencies, output them to the list and remove them as deps from others
mkdir 'packages';
while (my @nodeps = grep { not %{$_->{dep}} } values %final) {
	for my $package (@nodeps) {
		my $name = $package->{desc}->{Package};
		# Drop from the list of stuff to do
		delete $final{$name};
		for my $rdep (values %{$package->{revdep}}) {
			delete $rdep->{dep}->{$name}
		}
		# Handle the package
		my $filename = "$name-$package->{desc}->{Version}.ipk";
		die "Failed to download $name\n" if system 'wget', '-q', "$url/$package->{desc}->{Filename}", '-O', "packages/$filename";
		my $hash = Digest::SHA->new(256);
		$hash->addfile("packages/$filename");
		my $hash_result = $hash->hexdigest;
		my $flags = $desired{$name} // '.';
		print $output "$name\t$package->{desc}->{Version}\t$flags\t$hash_result\n";
		warn "Package $name should be encrypted, but that's not supported yet ‒ you need to encrypt manually\n" if $desired{$name} =~ /E/;
	}
}

# Make sure nothing is left
die "Circular dependencies in ", (join ", ", keys %final), "\n" if %final;

# Output the packages to remove
print $output map "$_\t-\t$desired{$_}\n", (grep { $desired{$_} =~ /R/ and $desired{$_} !~ /1/ } keys %desired);

close $output;
print $output_buffer;

my $hex = Digest::SHA::sha256_hex($output_buffer);
open my $signature, '|-', "openssl rsautl -sign -inkey '$key' -keyform PEM >$sigfilename" or die "Can't run openssl sign";
print $signature $hex, "\n";
close $signature;
