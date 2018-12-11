SOURCE1 = goenabled
SOURCE2 = e_nova-init
SOURCE3 = nova-cleanup
SOURCE4 = nova-startup
SOURCE5 = LICENSE

SOURCE11 = nova-cleanup.conf
SOURCE12 = nova-compute.conf
SOURCE14 = libvirtd.conf

SOURCE21 = nova-goenabled.sh
SOURCE22 = virt-support-goenabled.sh

SOURCE31 = goenabled-worker.service
SOURCE32 = e_nova-init.service


local_etc_pmond = $(_sysconfdir)/pmon.d
local_etc_goenabledd = $(_sysconfdir)/goenabled.d
local_etc_nova = $(_sysconfdir)/nova

.PHONY: default

install:
	# nfv-plugins
	# # Compute-Only Init Scripts (source group x)
	install -m 755 -p -D scripts/$(SOURCE1) $(buildroot)/$(_sysconfdir)/init.d/goenabledWorker
	install -m 755 -p -D scripts/$(SOURCE2) $(buildroot)/$(_sysconfdir)/init.d/e_nova-init
	install -m 755 -p -D scripts/$(SOURCE3) $(buildroot)/$(_sysconfdir)/init.d/nova-cleanup
	install -m 755 -p -D scripts/$(SOURCE4) $(buildroot)/$(_sysconfdir)/init.d/nova-startup
	# # Compute-Only Process Monitor Config files (source group 1x)
	install -m 755 -d $(buildroot)/$(local_etc_pmond)
	install -m 755 -d $(buildroot)/$(local_etc_nova)
	install -m 644 -p -D scripts/$(SOURCE11) $(buildroot)/$(local_etc_nova)/nova-cleanup.conf
	install -m 644 -p -D scripts/$(SOURCE12) $(buildroot)/$(local_etc_nova)/nova-compute.conf
	install -m 644 -p -D scripts/$(SOURCE14) $(buildroot)/$(local_etc_pmond)/libvirtd.conf
	# # Compute-Only Go Enabled Test (source group 2x)
	install -m 755 -d $(buildroot)/$(local_etc_goenabledd)
	install -m 755 -p -D scripts/$(SOURCE21) $(buildroot)/$(local_etc_goenabledd)/nova-goenabled.sh
	install -m 755 -p -D scripts/$(SOURCE22) $(buildroot)/$(local_etc_goenabledd)/virt-support-goenabled.sh
	# Install to systemd (source group 3x)
	install -m 644 -p -D scripts/$(SOURCE31) $(buildroot)/$(_unitdir)/goenabled-worker.service
	install -m 644 -p -D scripts/$(SOURCE32) $(buildroot)/$(_unitdir)/e_nova-init.service
	# for license
	install -m 755 -d $(buildroot)/$(_datarootdir)/licenses/mtce-compute-1.0
	install -p -D -m 600 $(SOURCE5) $(buildroot)/$(_datarootdir)/licenses/mtce-compute-1.0/LICENSE

