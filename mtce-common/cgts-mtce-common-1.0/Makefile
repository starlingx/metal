#
# Copyright (c) 2013-2016 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

VER=1
VER_MJR=1

build:
	@(cd public ; make lib VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd rmon/rmonApi ; make lib VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd daemon ; make lib VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd common ; make lib fsync VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd alarm  ; make build VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd heartbeat ; make build VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd maintenance ; make build VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd hwmon  ; make build VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd mtclog ; make build VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd pmon   ; make build VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd guest  ; make build VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd fsmon  ; make build VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd rmon   ; make build VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd rmon/rmon_resource_notify ; make build VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd hostw   ; make build VER=$(VER) VER_MJR=$(VER_MJR))

clean:
	@( cd public ; make clean )
	@( cd rmon/rmonApi ; make clean )
	@( cd daemon ; make clean )
	@( cd common ; make clean )
	@( cd alarm  ; make clean )
	@( cd mtclog ; make clean )
	@( cd hwmon  ; make clean )
	@( cd pmon   ; make clean )
	@( cd guest  ; make clean )
	@( cd fsmon  ; make clean )
	@( cd heartbeat ; make clean )
	@( cd maintenance ; make clean )
	@( cd rmon   ; make clean )
	@( cd rmon/rmon_resource_notify  ; make clean )
	@( cd hostw   ; make clean )
	@( rm -rf release )

backup: clean
	mkdir -p ~/mtce
	cp -a * ~/mtce
