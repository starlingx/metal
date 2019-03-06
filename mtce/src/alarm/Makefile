#
# Copyright (c) 2014-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SHELL = /bin/bash

SRCS = alarmInit.cpp alarmData.cpp alarmHdlr.cpp alarmMgr.cpp alarmUtil.cpp alarm.cpp
OBJS = $(SRCS:.cpp=.o)
LDLIBS = -lstdc++ -ldaemon -lcommon -lfmcommon -ljson-c -lrt -lcrypto -luuid
INCLUDES = -I. -I/usr/include/mtce-daemon -I/usr/include/mtce-common
INCLUDES += -I../smash -I../maintenance -I../public
CCFLAGS = -g -O2 -Wall -Wextra -Werror

STATIC_ANALYSIS_TOOL = cppcheck
STATIC_ANALYSIS_TOOL_EXISTS = $(shell [[ -e `which $(STATIC_ANALYSIS_TOOL)` ]] && echo 1 || echo 0)

BINS = mtcalarmd


.cpp.o:
	$(CXX) $(INCLUDES) $(CCFLAGS) $(EXTRACCFLAGS) -c $< -o $@

all: clean_bins static_analysis common daemon process library

static_analysis:
ifeq ($(STATIC_ANALYSIS_TOOL_EXISTS), 1)
	$(STATIC_ANALYSIS_TOOL) --language=c++ --enable=warning -U__AREA__ -DWANT_FIT_TESTING *.cpp *.h
else
	echo "Warning: '$(STATIC_ANALYSIS_TOOL)' static analysis tool not installed ; bypassing ..."
endif

build: clean_bins static_analysis ${OBJS} library process

common:
	( cd ../common ; make clean ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

daemon:
	( cd ../daemon ; make clean ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

library:
	 ar rcs libalarm.a alarm.o $(EXTRAARFLAGS)

process: $(OBJS)
	$(CXX) $(CCFLAGS) ${OBJS} -L../daemon -L../common $(LDLIBS) $(EXTRALDFLAGS) -o mtcalarmd

clean_bins:
	@rm -v -f $(BINS)

clean:
	@rm -v -f $(OBJ) $(BINS) *.o *.a

