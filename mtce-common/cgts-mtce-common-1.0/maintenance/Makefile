#
# Copyright (c) 2013-2016 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SRCS  = mtcAlarm.cpp
SRCS += mtcThreads.cpp
SRCS += mtcIpmiUtil.cpp
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
SRCS += mtcKeyApi.cpp
SRCS += mtcCmdHdlr.cpp
SRCS += mtcNodeMnfa.cpp
SRCS += mtcVimApi.cpp

COMPUTE_OBJS  = mtcNodeComp.o
COMPUTE_OBJS += mtcCompMsg.o

CONTROL_OBJS  = mtcAlarm.o
CONTROL_OBJS += mtcThreads.o
CONTROL_OBJS += mtcIpmiUtil.o
CONTROL_OBJS += mtcNodeCtrl.o
CONTROL_OBJS += mtcNodeFsm.o
CONTROL_OBJS += mtcNodeHdlrs.o
CONTROL_OBJS += mtcSubfHdlrs.o
CONTROL_OBJS += mtcCtrlMsg.o
CONTROL_OBJS += mtcWorkQueue.o
CONTROL_OBJS += mtcInvApi.o
CONTROL_OBJS += mtcSmgrApi.o
CONTROL_OBJS += mtcKeyApi.o
CONTROL_OBJS += mtcHttpUtil.o
CONTROL_OBJS += mtcHttpSvr.o
CONTROL_OBJS += mtcCmdHdlr.o
CONTROL_OBJS += mtcNodeMnfa.o
CONTROL_OBJS += mtcVimApi.o

OBJS = $(SRCS:.cpp=.o)
BINS = mtcAgent mtcClient
LDLIBS += -lstdc++ -ldaemon -lcommon -lthreadUtil -lipmiUtil -lfmcommon -lrmonapi -lalarm -lpthread -lrt -levent -ljson-c -lamon -lcrypto -luuid
INCLUDES += -I../common -I../daemon -I../alarm -I../heartbeat -I../hwmon -I../public -I../rmon/rmonApi -I.
CCFLAGS += -g -O2 -Wall -Wextra -Werror -Wno-missing-braces

STATIC_ANALYSIS_TOOL = cppcheck
STATIC_ANALYSIS_TOOL_EXISTS = $(shell [[ -e `which $(STATIC_ANALYSIS_TOOL)` ]] && echo 1 || echo 0)

.cpp.o:
	$(CXX) $(INCLUDES) $(CCFLAGS) $(EXTRACCFLAGS) -c $< -o $@

all: remove_bins $(OBJS) $(BINS)

static_analysis:
ifeq ($(STATIC_ANALYSIS_TOOL_EXISTS), 1)
	$(STATIC_ANALYSIS_TOOL) --enable=warning --language=c++ -U__AREA__ -UWANT_FIT_TESTING *.cpp *.h
else
	echo "Warning: '$(STATIC_ANALYSIS_TOOL)' static analysis tool not installed ; bypassing ..."
endif

build: clean static_analysis remove_bins $(OBJS) $(BINS)

mtcAgent: $(OBJS)
	$(CXX) $(CONTROL_OBJS) -L../daemon -L../common -L../public -L../alarm -L../rmon/rmonApi $(LDLIBS) -o mtcAgent

mtcClient: $(OBJS)
	$(CXX) $(COMPUTE_OBJS) -L../daemon -L../common -L../public -L../alarm -L../rmon/rmonApi $(LDLIBS) -o mtcClient

remove_bins:
	@rm -f $(BINS)

clean:
	@rm -f $(OBJS) $(BINS)
