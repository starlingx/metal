#
# Copyright (c) 2014-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SHELL = /bin/bash

SRCS += hwmonUtil.cpp
SRCS += hwmonIpmi.cpp
SRCS += hwmonModel.cpp
SRCS += hwmonGroup.cpp
SRCS += hwmonSensor.cpp
SRCS += hwmonThreads.cpp
SRCS += hwmonAlarm.cpp
SRCS += hwmonHdlr.cpp
SRCS += hwmonJson.cpp
SRCS += hwmonClass.cpp
SRCS += hwmonHttp.cpp
SRCS += hwmonInit.cpp
SRCS += hwmonMsg.cpp
SRCS += hwmonFsm.cpp

OBJS = $(SRCS:.cpp=.o)
BIN = hwmond
LDLIBS = -lstdc++ -ldaemon -lfmcommon -lcommon -lthreadUtil -lipmiUtil -lpthread -levent -ljson-c -lrt -lcrypto 
INCLUDES = -I. -I/usr/include/mtce-daemon -I/usr/include/mtce-common
INCLUDES += -I../maintenance
CCFLAGS = -g -O2 -Wall -Wextra -Werror -std=c++11 -pthread

STATIC_ANALYSIS_TOOL = cppcheck
STATIC_ANALYSIS_TOOL_EXISTS = $(shell [[ -e `which $(STATIC_ANALYSIS_TOOL)` ]] && echo 1 || echo 0)

# all: common daemon build clean_common
all: clean static_analysis common daemon build

.cpp.o:
	$(CXX) $(INCLUDES) $(CCFLAGS) $(EXTRACCFLAGS) -c $< -o $@

static_analysis:
ifeq ($(STATIC_ANALYSIS_TOOL_EXISTS), 1)
	$(STATIC_ANALYSIS_TOOL) --enable=warning --language=c++ -U__AREA__ -DWANT_FIT_TESTING *.cpp *.h
else
	echo "Warning: '$(STATIC_ANALYSIS_TOOL)' static analysis tool not installed ; bypassing ..."
endif

build: clean static_analysis $(OBJS)
	$(CXX) $(CCFLAGS) $(OBJS) -L../daemon -L../common $(LDLIBS) -o $(BIN)

common:
	( cd ../common ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

daemon:
	( cd ../daemon ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

clean_bins:
	@rm -v -f $(BIN)

clean:
	@rm -v -f $(OBJS) $(BIN) 
