SOURCE1 = LICENSE
SOURCE2 = goenabled
SOURCE3 = hbsAgent
SOURCE4 = hbsAgent.conf
SOURCE5 = hbsAgent.service

PMONDIR ?= $(_sysconfdir)/pmon.d
local_etc_goenabledd = $(_sysconfdir)/goenabled.d

.PHONY: default

install:

	# for license
	install -m 755 -d $(buildroot)/$(_datarootdir)/licenses/mtce-control-1.0
	install -m 600 -p -D $(SOURCE1) $(buildroot)/$(_datarootdir)/licenses/mtce-control-1.0/LICENSE

	# Controller-Only Init Scripts
	install -m 755 -d $(buildroot)/$(_sysconfdir)/init.d
	install -m 755 -p -D scripts/$(SOURCE2) $(buildroot)/$(_sysconfdir)/init.d/goenabledControl
	install -m 755 -p -D scripts/$(SOURCE3) $(buildroot)/$(_sysconfdir)/init.d/hbsAgent

	# Controller-Only Process Monitor Config files
	install -m 755 -d $(buildroot)/$(PMONDIR)
	install -m 644 -p -D scripts/$(SOURCE4) $(buildroot)/$(PMONDIR)/hbsAgent.conf

	# Controller-Only Heartbeat Service file
	install -m 644 -p -D scripts/$(SOURCE5) $(buildroot)/$(_unitdir)/hbsAgent.service

	# Controller-Only Go Enabled Test
	install -m 755 -d $(buildroot)/$(local_etc_goenabledd)
