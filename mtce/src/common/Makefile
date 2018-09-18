#
# Copyright (c) 2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SRCS = nodeClass.cpp
OBJS = $(SRCS:.cpp=.o)


INCLUDES = -I. -I/usr/include/mtce-daemon -I/usr/include/mtce-common
INCLUDES += -I../alarm -I../heartbeat -I../maintenance
CCFLAGS = -g -O2 -Wall -Wextra -Werror -std=c++11

STATIC_ANALYSIS_TOOL = cppcheck
STATIC_ANALYSIS_TOOL_EXISTS = $(shell [[ -e `which $(STATIC_ANALYSIS_TOOL)` ]] && echo 1 || echo 0)

ifeq (,$(shell which ${CC}))
CC=g++
endif

.cpp.o:
	$(CXX) $(CCFLAGS) $(INCLUDES) $(EXTRACCFLAGS) -c $< -o $@

static_analysis:
ifeq ($(STATIC_ANALYSIS_TOOL_EXISTS), 1)
	$(STATIC_ANALYSIS_TOOL) --language=c++ --enable=warning -U__AREA__ -UWANT_FIT_TESTING *.cpp *.h
else
	echo "Warning: '$(STATIC_ANALYSIS_TOOL)' static analysis tool not installed ; bypassing ..."
endif

lib: clean $(OBJS) library

build: static_analysis $(OBJS) library

library:
	ar rcs libmtcecommon.a $(COMMON_OBJS) $(EXTRAARFLAGS)

clean:
	@rm -v -f *.o *.a
