SOURCE1 = goenabled
SOURCE5 = LICENSE

SOURCE22 = virt-support-goenabled.sh

SOURCE31 = goenabled-worker.service


local_etc_pmond = $(_sysconfdir)/pmon.d
local_etc_goenabledd = $(_sysconfdir)/goenabled.d

.PHONY: default

install:
	# nfv-plugins
	# # Compute-Only Init Scripts (source group x)
	install -m 755 -p -D scripts/$(SOURCE1) $(buildroot)/$(_sysconfdir)/init.d/goenabledWorker
	# # Compute-Only Process Monitor Config files (source group 1x)
	install -m 755 -d $(buildroot)/$(local_etc_pmond)
	# # Compute-Only Go Enabled Test (source group 2x)
	install -m 755 -d $(buildroot)/$(local_etc_goenabledd)
	install -m 755 -p -D scripts/$(SOURCE22) $(buildroot)/$(local_etc_goenabledd)/virt-support-goenabled.sh
	# Install to systemd (source group 3x)
	install -m 644 -p -D scripts/$(SOURCE31) $(buildroot)/$(_unitdir)/goenabled-worker.service
	# for license
	install -m 755 -d $(buildroot)/$(_datarootdir)/licenses/mtce-compute-1.0
	install -p -D -m 600 $(SOURCE5) $(buildroot)/$(_datarootdir)/licenses/mtce-compute-1.0/LICENSE

