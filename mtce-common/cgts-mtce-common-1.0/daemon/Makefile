#
# Copyright (c) 2013-2015 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SRCS = daemon_main.cpp daemon_ini.cpp daemon_debug.cpp daemon_signal.cpp daemon_config.cpp daemon_files.cpp 
OBJS = daemon_main.o   daemon_ini.o   daemon_debug.o   daemon_signal.o   daemon_config.o   daemon_files.o

OBJS = $(SRCS:.cpp=.o)
INCLUDES = -I. -I../common
CCFLAGS = -g -O2 -Wall -Wextra -Werror -DBUILDINFO="\"$$(date)\""

STATIC_ANALYSIS_TOOL = cppcheck
STATIC_ANALYSIS_TOOL_EXISTS = $(shell [[ -e `which $(STATIC_ANALYSIS_TOOL)` ]] && echo 1 || echo 0)

.cpp.o:
	$(CXX) $(INCLUDES) $(CCFLAGS) -lstdc++ $(EXTRACCFLAGS) -c $< -o $@

static_analysis:
ifeq ($(STATIC_ANALYSIS_TOOL_EXISTS), 1)
	$(STATIC_ANALYSIS_TOOL) --language=c++ --enable=warning -U__AREA__ -UWANT_FIT_TESTING *.cpp *.h
else
	echo "Warning: '$(STATIC_ANALYSIS_TOOL)' static analysis tool not installed ; bypassing ..."
endif

lib: build $(OBJS)
	ar rcs libdaemon.a $(OBJS) $(EXTRAARFLAGS)

build: clean static_analysis

clean:
	@rm -f $(OBJ) *.o *.a
