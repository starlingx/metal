#
# Copyright (c) 2013-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SHELL = /bin/bash

SRCS  = mtcAlarm.cpp
SRCS += mtcThreads.cpp
SRCS += mtcBmcUtil.cpp
SRCS += mtcNodeHdlrs.cpp
SRCS += mtcSubfHdlrs.cpp
SRCS += mtcNodeFsm.cpp
SRCS += mtcCompMsg.cpp
SRCS += mtcCtrlMsg.cpp
SRCS += mtcNodeCtrl.cpp
SRCS += mtcNodeComp.cpp
SRCS += mtcHttpUtil.cpp
SRCS += mtcHttpSvr.cpp
SRCS += mtcWorkQueue.cpp
SRCS += mtcInvApi.cpp
SRCS += mtcSmgrApi.cpp
SRCS += mtcCmdHdlr.cpp
SRCS += mtcNodeMnfa.cpp
SRCS += mtcVimApi.cpp
SRCS += mtcStubs.cpp

COMPUTE_OBJS  = mtcNodeComp.o
COMPUTE_OBJS += mtcCompMsg.o
CONTROL_OBJS += ../common/nodeClass.o

CONTROL_OBJS  = mtcAlarm.o
CONTROL_OBJS += mtcThreads.o
CONTROL_OBJS += mtcBmcUtil.o
CONTROL_OBJS += mtcNodeCtrl.o
CONTROL_OBJS += mtcNodeFsm.o
CONTROL_OBJS += mtcNodeHdlrs.o
CONTROL_OBJS += mtcSubfHdlrs.o
CONTROL_OBJS += mtcCtrlMsg.o
CONTROL_OBJS += mtcWorkQueue.o
CONTROL_OBJS += mtcInvApi.o
CONTROL_OBJS += mtcSmgrApi.o
CONTROL_OBJS += mtcHttpUtil.o
CONTROL_OBJS += mtcHttpSvr.o
CONTROL_OBJS += mtcCmdHdlr.o
CONTROL_OBJS += mtcNodeMnfa.o
CONTROL_OBJS += mtcVimApi.o
CONTROL_OBJS += mtcStubs.o
CONTROL_OBJS += ../common/nodeClass.o

OBJS = $(SRCS:.cpp=.o)
BINS = mtcAgent mtcClient
LDLIBS += -lstdc++ -ldaemon -lcommon -lthreadUtil -lbmcUtils -lfmcommon -lalarm -lpthread -lrt -levent -ljson-c -lamon -lcrypto -luuid
INCLUDES = -I. -I/usr/include/mtce-daemon -I/usr/include/mtce-common
INCLUDES += -I../common -I../alarm -I../heartbeat -I../hwmon -I../public
CCFLAGS += -g -O2 -Wall -Wextra -Werror -Wno-missing-braces -std=c++11

STATIC_ANALYSIS_TOOL = cppcheck
STATIC_ANALYSIS_TOOL_EXISTS = $(shell [[ -e `which $(STATIC_ANALYSIS_TOOL)` ]] && echo 1 || echo 0)

.cpp.o:
	$(CXX) $(INCLUDES) $(CCFLAGS) $(EXTRACCFLAGS) -c $< -o $@

all: common remove_bins $(OBJS) $(BINS)

static_analysis:
ifeq ($(STATIC_ANALYSIS_TOOL_EXISTS), 1)
	$(STATIC_ANALYSIS_TOOL) --enable=warning --language=c++ -U__AREA__ -UWANT_FIT_TESTING *.cpp *.h
else
	echo "Warning: '$(STATIC_ANALYSIS_TOOL)' static analysis tool not installed ; bypassing ..."
endif

build: static_analysis remove_bins $(OBJS) $(BINS)

common:
	( cd ../common ; make clean ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

mtcAgent: $(OBJS)
	$(CXX) $(CONTROL_OBJS) -L../public -L../alarm $(LDLIBS) $(EXTRALDFLAGS) -o mtcAgent

mtcClient: $(OBJS)
	$(CXX) $(COMPUTE_OBJS) -L../public -L../alarm $(LDLIBS) $(EXTRALDFLAGS) -o mtcClient

remove_bins:
	@rm -v -f $(BINS)

clean_common:
	@rm -v -f ../common/*.o ../common/*.a

clean: clean_common
	@rm -v -f $(OBJS) $(BINS)
