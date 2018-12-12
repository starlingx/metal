#
# Copyright (c) 2014-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SHELL = /bin/bash

SRCS = rmonInit.cpp rmonMsg.cpp rmonHdlr.cpp rmonHttp.cpp interfaceHdlr.cpp rmonFM.cpp remoteloggingHdlr.cpp thinmetaHdlr.cpp
OBJS = $(SRCS:.cpp=.o)
LDLIBS = -lstdc++ -ldaemon -lcommon -lfmcommon -lrmonapi -lrt -levent -lcrypto -luuid -lpthread -ljson-c
INCLUDES = -I. -I/usr/include/mtce-daemon -I/usr/include/mtce-common
INCLUDES += -IrmonApi
CCFLAGS = -g -O2 -Wall -Werror -Wextra

STATIC_ANALYSIS_TOOL = cppcheck
STATIC_ANALYSIS_TOOL_EXISTS = $(shell [[ -e `which $(STATIC_ANALYSIS_TOOL)` ]] && echo 1 || echo 0)

all: clean static_analysis common daemon build

.cpp.o:
	$(CXX) $(INCLUDES) $(CCFLAGS) $(EXTRACCFLAGS) -c $< -o $@

static_analysis:
ifeq ($(STATIC_ANALYSIS_TOOL_EXISTS), 1)
	$(STATIC_ANALYSIS_TOOL) --language=c++ --enable=warning -U__AREA__ -DWANT_FIT_TESTING *.cpp *.h
else
	echo "Warning: '$(STATIC_ANALYSIS_TOOL)' static analysis tool not installed ; bypassing ..."
endif

build: clean static_analysis $(OBJS)
	$(CXX) $(CCFLAGS) $(OBJS) -L../daemon -L../common -LrmonApi/ $(LDLIBS) -o rmond

lib: build $(OBJS)

common:
	( cd ../common ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

daemon:
	( cd ../daemon ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

clean:
	@rm -v -f $(OBJ) rmond *.o *.a
