SOURCE1 = goenabled
SOURCE2 = LICENSE

local_etc_pmond = $(_sysconfdir)/pmond.d
local_etc_goenabledd = $(_sysconfdir)/goenabled.d

.PHONY: default

install:
	# Controller-Only Init Scripts
	install -m 755 -p -D scripts/$(SOURCE1) $(buildroot)/$(_sysconfdir)/init.d/goenabledControl
	# Controller-Only Process Monitor Config files
	install -m 755 -d $(buildroot)/$(local_etc_pmond)
	# Controller-Only Go Enabled Test
	install -m 755 -d $(buildroot)/$(local_etc_goenabledd)
	# for license
	install -m 755 -d $(buildroot)/$(_datarootdir)/licenses/mtce-control-1.0
	install -p -D -m 600 $(SOURCE2) $(buildroot)/$(_datarootdir)/licenses/mtce-control-1.0/LICENSE

