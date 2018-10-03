#
# Copyright (c) 2013-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SRCS = hbsAlarm.cpp hbsClient.cpp hbsAgent.cpp hbsPmon.cpp hbsUtil.cpp hbsCluster.cpp hbsStubs.cpp
OBJS = $(SRCS:.cpp=.o)

LDLIBS = -lstdc++ -ldaemon -lcommon -lthreadUtil -lpthread -lfmcommon -lalarm -lrt -lamon -lcrypto -luuid -ljson-c
INCLUDES = -I. -I/usr/include/mtce-daemon -I/usr/include/mtce-common
INCLUDES += -I../common -I../alarm -I../maintenance -I../public

CCFLAGS = -g -O2 -Wall -Wextra -Werror

STATIC_ANALYSIS_TOOL = cppcheck
STATIC_ANALYSIS_TOOL_EXISTS = $(shell [[ -e `which $(STATIC_ANALYSIS_TOOL)` ]] && echo 1 || echo 0)

BINS = hbsAgent hbsClient

.cpp.o:
	$(CXX) $(INCLUDES) $(CCFLAGS) $(EXTRACCFLAGS) -c $< -o $@

static_analysis:
ifeq ($(STATIC_ANALYSIS_TOOL_EXISTS), 1)
	$(STATIC_ANALYSIS_TOOL) --language=c++ --enable=warning -U__AREA__ -DWANT_FIT_TESTING *.cpp *.h
else
	echo "Warning: '$(STATIC_ANALYSIS_TOOL)' static analysis tool not installed ; bypassing ..."
endif

all: static_analysis common agent client

build: static_analysis $(OBJS)
	$(CXX) $(CCFLAGS) hbsAlarm.o hbsAgent.o hbsUtil.o hbsCluster.o hbsStubs.o ../common/nodeClass.o -L../public -L../alarm $(LDLIBS) -o hbsAgent
	$(CXX) $(CCFLAGS) hbsClient.o hbsPmon.o hbsUtil.o -L../public -L../alarm $(LDLIBS) -o hbsClient

common:
	( cd ../common ; make clean ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

agent: $(OBJS)
	$(CXX) $(CCFLAGS) hbsAgent.o -L../alarm -L../public $(LDLIBS) -o hbsAgent
	
client: $(OBJS)
	$(CXX) $(CCFLAGS) hbsClient.o -L../public $(LDLIBS) -o hbsClient

clean_bins:
	@rm -v -f $(BINS)

clean:
	@rm -v -f $(OBJS) $(BINS) *.o *.a

