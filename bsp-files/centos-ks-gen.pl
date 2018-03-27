#!/usr/bin/perl
#
# Copyright (c) 2017 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

use strict;
use Getopt::Long;
use POSIX qw(strftime);

# Defines the current list of YOW boot servers
my %boot_servers = ("yow-tuxlab", "128.224.150.9",
                    "yow-tuxlab2", "128.224.151.254",
                    "yow", "128.224.150.9");  # obsolete; kept for backwards compatibility

my $PLATFORM_RELEASE;
my $files_dir;
my $output_dir = 'generated';
my $pxeboot_output_dir = 'pxeboot';
my $extra_output_dir = 'extra_cfgs';

GetOptions("release=s" => \$PLATFORM_RELEASE,
           "basedir=s" => \$files_dir);

die "Please specify release with --release" if (!$PLATFORM_RELEASE);
if (!$files_dir)
{
    $files_dir = '.';
}

my $BOOT_SERVER = "none";

my $template_dir = "$files_dir/kickstarts";

system("mkdir -p ${output_dir}");

# Write USB image files
write_config_file("controller",
                  "${output_dir}/controller_ks.cfg", "filter_out_from_controller",
                  "pre_common_head.cfg",
                  "pre_pkglist.cfg",
                  "pre_disk_setup_common.cfg",
                  "pre_disk_controller.cfg",
                  "post_platform_conf_controller.cfg",
                  "post_common.cfg",
                  "post_kernel_controller.cfg",
                  "post_lvm_pv_on_rootfs.cfg",
                  "post_usb_controller.cfg");
write_config_file("controller-compute",
                  "${output_dir}/smallsystem_ks.cfg", "filter_out_from_smallsystem",
                  "pre_common_head.cfg",
                  "pre_pkglist.cfg",
                  "pre_disk_setup_common.cfg",
                  "pre_disk_aio.cfg",
                  "post_platform_conf_aio.cfg",
                  "post_common.cfg",
                  "post_kernel_aio_and_compute.cfg",
                  "post_lvm_pv_on_rootfs.cfg",
                  "post_system_aio.cfg",
                  "post_usb_controller.cfg");
write_config_file("controller-compute-lowlatency",
                  "${output_dir}/smallsystem_lowlatency_ks.cfg", "filter_out_from_smallsystem_lowlatency",
                  "pre_common_head.cfg",
                  "pre_pkglist_lowlatency.cfg",
                  "pre_disk_setup_common.cfg",
                  "pre_disk_aio.cfg",
                  "post_platform_conf_aio_lowlatency.cfg",
                  "post_common.cfg",
                  "post_kernel_aio_and_compute.cfg",
                  "post_lvm_pv_on_rootfs.cfg",
                  "post_system_aio.cfg",
                  "post_usb_controller.cfg");

system("mkdir -p ${pxeboot_output_dir}");

# Write PXE boot files
write_config_file("controller",
                  "${pxeboot_output_dir}/pxeboot_controller.cfg", "filter_out_from_controller",
                  "pre_common_head.cfg",
                  "pre_pkglist.cfg",
                  "pre_disk_setup_common.cfg",
                  "pre_disk_controller.cfg",
                  "post_platform_conf_controller.cfg",
                  "post_common.cfg",
                  "post_kernel_controller.cfg",
                  "post_lvm_pv_on_rootfs.cfg",
                  "post_pxeboot_controller.cfg");
write_config_file("controller-compute",
                  "${pxeboot_output_dir}/pxeboot_smallsystem.cfg", "filter_out_from_smallsystem",
                  "pre_common_head.cfg",
                  "pre_pkglist.cfg",
                  "pre_disk_setup_common.cfg",
                  "pre_disk_aio.cfg",
                  "post_platform_conf_aio.cfg",
                  "post_common.cfg",
                  "post_kernel_aio_and_compute.cfg",
                  "post_lvm_pv_on_rootfs.cfg",
                  "post_system_aio.cfg",
                  "post_pxeboot_controller.cfg");
write_config_file("controller-compute-lowlatency",
                  "${pxeboot_output_dir}/pxeboot_smallsystem_lowlatency.cfg", "filter_out_from_smallsystem_lowlatency",
                  "pre_common_head.cfg",
                  "pre_pkglist_lowlatency.cfg",
                  "pre_disk_setup_common.cfg",
                  "pre_disk_aio.cfg",
                  "post_platform_conf_aio_lowlatency.cfg",
                  "post_common.cfg",
                  "post_kernel_aio_and_compute.cfg",
                  "post_lvm_pv_on_rootfs.cfg",
                  "post_system_aio.cfg",
                  "post_pxeboot_controller.cfg");


# Write same net files
write_config_file("controller",
                  "${output_dir}/net_controller_ks.cfg", "filter_out_from_controller",
                  "pre_common_head.cfg",
                  "pre_net_common.cfg",
                  "pre_pkglist.cfg",
                  "pre_disk_setup_common.cfg",
                  "pre_disk_controller.cfg",
                  "post_platform_conf_controller.cfg",
                  "post_common.cfg",
                  "post_kernel_controller.cfg",
                  "post_lvm_pv_on_rootfs.cfg",
                  "post_net_controller.cfg",
                  "post_net_common.cfg");
write_config_file("controller-compute",
                  "${output_dir}/net_smallsystem_ks.cfg", "filter_out_from_smallsystem",
                  "pre_common_head.cfg",
                  "pre_net_common.cfg",
                  "pre_pkglist.cfg",
                  "pre_disk_setup_common.cfg",
                  "pre_disk_aio.cfg",
                  "post_platform_conf_aio.cfg",
                  "post_common.cfg",
                  "post_kernel_aio_and_compute.cfg",
                  "post_lvm_pv_on_rootfs.cfg",
                  "post_system_aio.cfg",
                  "post_net_controller.cfg",
                  "post_net_common.cfg");
write_config_file("controller-compute-lowlatency",
                  "${output_dir}/net_smallsystem_lowlatency_ks.cfg", "filter_out_from_smallsystem_lowlatency",
                  "pre_common_head.cfg",
                  "pre_net_common.cfg",
                  "pre_pkglist_lowlatency.cfg",
                  "pre_disk_setup_common.cfg",
                  "pre_disk_aio.cfg",
                  "post_platform_conf_aio_lowlatency.cfg",
                  "post_common.cfg",
                  "post_kernel_aio_and_compute.cfg",
                  "post_lvm_pv_on_rootfs.cfg",
                  "post_system_aio.cfg",
                  "post_net_controller.cfg",
                  "post_net_common.cfg");
write_config_file("compute",
                  "${output_dir}/net_compute_ks.cfg", "filter_out_from_compute",
                  "pre_common_head.cfg",
                  "pre_net_common.cfg",
                  "pre_pkglist.cfg",
                  "pre_disk_setup_common.cfg",
                  "pre_disk_compute.cfg",
                  "post_platform_conf_compute.cfg",
                  "post_common.cfg",
                  "post_kernel_aio_and_compute.cfg",
                  "post_lvm_no_pv_on_rootfs.cfg",
                  "post_net_common.cfg");
write_config_file("compute-lowlatency",
                  "${output_dir}/net_compute_lowlatency_ks.cfg", "filter_out_from_compute_lowlatency",
                  "pre_common_head.cfg",
                  "pre_net_common.cfg",
                  "pre_pkglist_lowlatency.cfg",
                  "pre_disk_setup_common.cfg",
                  "pre_disk_compute.cfg",
                  "post_platform_conf_compute_lowlatency.cfg",
                  "post_common.cfg",
                  "post_kernel_aio_and_compute.cfg",
                  "post_lvm_no_pv_on_rootfs.cfg",
                  "post_net_common.cfg");
write_config_file("storage",
                  "${output_dir}/net_storage_ks.cfg", "filter_out_from_storage",
                  "pre_common_head.cfg",
                  "pre_net_common.cfg",
                  "pre_pkglist.cfg",
                  "pre_disk_setup_common.cfg",
                  "pre_disk_storage.cfg",
                  "post_platform_conf_storage.cfg",
                  "post_common.cfg",
                  "post_kernel_storage.cfg",
                  "post_lvm_pv_on_rootfs.cfg",
                  "post_net_common.cfg");

system("mkdir -p ${extra_output_dir}");

# write Ottawa Lab files
my $server;
foreach $server (keys %boot_servers)
{
    $BOOT_SERVER = $boot_servers{$server};

    write_config_file("controller",
                      "${extra_output_dir}/${server}_controller.cfg", "filter_out_from_controller",
                      "pre_common_head.cfg",
                      "pre_pkglist.cfg",
                      "pre_disk_setup_common.cfg",
                      "pre_disk_controller.cfg",
                      "post_platform_conf_controller.cfg",
                      "post_common.cfg",
                      "post_kernel_controller.cfg",
                      "post_lvm_pv_on_rootfs.cfg",
                      "post_yow_controller.cfg");
    write_config_file("controller-compute",
                      "${extra_output_dir}/${server}_smallsystem.cfg", "filter_out_from_smallsystem",
                      "pre_common_head.cfg",
                      "pre_pkglist.cfg",
                      "pre_disk_setup_common.cfg",
                      "pre_disk_aio.cfg",
                      "post_platform_conf_aio.cfg",
                      "post_common.cfg",
                      "post_kernel_aio_and_compute.cfg",
                      "post_lvm_pv_on_rootfs.cfg",
                      "post_system_aio.cfg",
                      "post_yow_controller.cfg");
    write_config_file("controller-compute-lowlatency",
                      "${extra_output_dir}/${server}_smallsystem_lowlatency.cfg", "filter_out_from_smallsystem_lowlatency",
                      "pre_common_head.cfg",
                      "pre_pkglist_lowlatency.cfg",
                      "pre_disk_setup_common.cfg",
                      "pre_disk_aio.cfg",
                      "post_platform_conf_aio_lowlatency.cfg",
                      "post_common.cfg",
                      "post_kernel_aio_and_compute.cfg",
                      "post_lvm_pv_on_rootfs.cfg",
                      "post_system_aio.cfg",
                      "post_yow_controller.cfg");
}

exit 0;

#------------------------#

sub write_config_file {
    my ($personality, $ksout, $filter_file, @templates) = @_;
    my %filter;
    if ($filter_file ne "") {
        if (!(open(FILTER, "$files_dir/$filter_file"))) {
            die "Could not open template $files_dir/$filter_file";
        }
        while (<FILTER>) {
            chop();
            next if ($_ =~ /^#/);
            $filter{$_} = 1;
        }
        close(FILTER);
    }
    print "Writing: $ksout\n";
    open(OUT, ">$ksout") || die "Could not write $ksout";

    my $year = strftime "%Y", localtime;
    print OUT "#\n";
    print OUT "# Copyright (c) $year Wind River Systems, Inc.\n";
    print OUT "# SPDX-License-Identifier: Apache-2.0\n";
    print OUT "#\n";
    print OUT "\n";

    # Add functions header
    foreach my $block ("\%pre", "\%post") {
        if (!(open(FUNCTIONS, "$template_dir/functions.sh"))) {
            die "Could not open functions.sh";
        }
        print OUT "$block\n";
        while (<FUNCTIONS>) {
            s/xxxPLATFORM_RELEASExxx/$PLATFORM_RELEASE/g;
            s/xxxBOOT_SERVERxxx/$BOOT_SERVER/g;
            s/xxxYEARxxx/$year/g;
            print OUT $_;
        }
        print OUT "\%end\n\n";
        close FUNCTIONS;
    }

    my $template;
    foreach $template (@templates) {
        if (!(open(TEMPLATE_IN, "$template_dir/$template"))) {
            die "Could not open template $template_dir/$template";
        }
        print OUT "\n# Template from: $template\n";
        while (<TEMPLATE_IN>) {
            $_ =~ s/\n$//;
            s/xxxPLATFORM_RELEASExxx/$PLATFORM_RELEASE/g;
            s/xxxBOOT_SERVERxxx/$BOOT_SERVER/g;
            s/xxxYEARxxx/$year/g;

            s/xxxPACKAGE_LISTxxx/\@platform-$personality\n\@updates-$personality/;

            print OUT "$_\n";
        }
        close(TEMPLATE_IN);
    }

    close(OUT);
}
