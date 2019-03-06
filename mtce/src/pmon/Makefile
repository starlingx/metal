#
# Copyright (c) 2014-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SHELL = /bin/bash

SRCS = pmonAlarm.cpp pmonInit.cpp pmonMsg.cpp pmonHdlr.cpp pmonFsm.cpp
OBJS = $(SRCS:.cpp=.o)
LDLIBS = -lstdc++ -ldaemon -lcommon -lrt -lcrypto -lfmcommon -ljson-c
INCLUDES = -I. -I/usr/include/mtce-daemon -I/usr/include/mtce-common
INCLUDES += -I../hostw
CCFLAGS = -g -O2 -Wall -Wextra -Werror

STATIC_ANALYSIS_TOOL = cppcheck
STATIC_ANALYSIS_TOOL_EXISTS = $(shell [[ -e `which $(STATIC_ANALYSIS_TOOL)` ]] && echo 1 || echo 0)

all: clean static_analysis common daemon build clean_common

.cpp.o:
	$(CXX) $(INCLUDES) $(CCFLAGS) $(EXTRACCFLAGS) -c $< -o $@

static_analysis:
ifeq ($(STATIC_ANALYSIS_TOOL_EXISTS), 1)
	$(STATIC_ANALYSIS_TOOL) --language=c++ --enable=warning -U__AREA__ *.cpp *.h
else
	echo "Warning: '$(STATIC_ANALYSIS_TOOL)' static analysis tool not installed ; bypassing ..."
endif

build: clean static_analysis $(OBJS)
	$(CXX) $(CCFLAGS) $(OBJS) -L../daemon -L../common $(LDLIBS) $(EXTRALDFLAGS) -o pmond

common:
	( cd ../common ; make clean ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

daemon:
	( cd ../daemon ; make clean ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

clean_common:
	@rm -v -f ../common/*.o
	@rm -v -f ../daemon/*.o

clean: clean_common
	@rm -v -f $(OBJ) pmond *.o *.a
