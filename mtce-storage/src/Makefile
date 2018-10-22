SOURCE1 = goenabled
SOURCE2 = goenabled-storage.service
SOURCE3 = LICENSE

local_etc_pmond = $(_sysconfdir)/pmond.d
local_etc_goenabledd = $(_sysconfdir)/goenabled.d
local_etc_servicesd  = $(_sysconfdir)/services.d

.PHONY: default

install:
	# Storage-Only Init Scripts
	install -m 755 -p -D scripts/$(SOURCE1) $(buildroot)/$(_sysconfdir)/init.d/goenabledStorage
	# Storage-Only Process Monitor Config files
	install -m 755 -d $(buildroot)/$(local_etc_pmond)
	# Storage-Only Go Enabled Tests
	install -m 755 -d $(buildroot)/$(local_etc_goenabledd)
	# Storage-Only Services
	install -m 755 -d $(buildroot)/$(local_etc_servicesd)/storage
	# Install systemd dir
	install -m 644 -p -D scripts/$(SOURCE2) $(buildroot)/$(_unitdir)/goenabled-storage.service
	# for license
	install -m 755 -d $(buildroot)/$(_datarootdir)/licenses/mtce-storage-1.0
	install -p -D -m 600 scripts/$(SOURCE3) $(buildroot)/$(_datarootdir)/licenses/mtce-storage-1.0/LICENSE

