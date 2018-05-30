#!/usr/bin/perl
use strict;

#my $file_in = `ls -tr bitbake_build/tmp/work/*/*/*/installed_pkgs.txt |tail -1`;
my $file_in = `ls -tr bitbake_build/tmp/work/intel_x86_64-wrs-linux/wrlinux-image-cgcs-base/*/installed_pkgs.txt |tail -1`;
$file_in = $ARGV[0] if ($ARGV[0] ne "");

open(FILES_IN, $file_in) || die "Could not open list of files";

my %pkgs;
while (<FILES_IN>) {
    chop;
    my @v = split();
    my $pkg = $v[0];
    my $arch = $v[1];
    # Fix up any lib32 packages
    if ($pkg =~ /lib32-(.*)$/) {
        $pkg = "$1";
        $arch = "lib32_x86";
    }
    $pkgs{"$pkg.$arch"} = "1";
}
close(FILES_IN);

# Print all packages in sorted unique order with architecture
foreach (sort keys %pkgs) {
    if (/(.*)\.(.*)/) {
        my $pkg = $1;
        my $arch = $2;
        print "$pkg $arch\n";
    }
}

exit 0;
