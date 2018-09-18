#
# Copyright (c) 2013-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

VER=1
VER_MJR=1

build:
	@(cd daemon ; make lib VER=$(VER) VER_MJR=$(VER_MJR))
	@(cd common ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

clean:
	@( cd daemon ; make clean )
	@( cd common ; make clean )
	@( rm -rf release )

