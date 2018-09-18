#
# Copyright (c) 2013-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SRCS = regexUtil.cpp \
	   timeUtil.cpp \
	   ipmiUtil.cpp \
	   pingUtil.cpp \
	   keyClass.cpp \
	   hostClass.cpp \
	   nodeBase.cpp \
	   nodeTimers.cpp \
	   nodeUtil.cpp \
	   hostUtil.cpp \
	   nodeEvent.cpp \
	   alarmUtil.cpp \
	   nlEvent.cpp \
	   jsonUtil.cpp \
	   httpUtil.cpp \
	   tokenUtil.cpp \
	   msgClass.cpp

COMMON_OBJS = regexUtil.o \
	   timeUtil.o \
	   pingUtil.o \
	   keyClass.o \
	   hostClass.o \
	   nodeBase.o \
	   nodeTimers.o \
	   nodeUtil.o \
	   hostUtil.o \
	   nodeEvent.o \
	   alarmUtil.o \
	   nlEvent.o \
	   jsonUtil.o \
	   httpUtil.o \
	   tokenUtil.o \
	   msgClass.o

OBJS = $(SRCS:.cpp=.o)
LDLIBS += -lstdc++ -ldaemon -lcommon -lfmcommon -lrt -lpq -levent -ljson-c -lcrypto -luuid

INCLUDES = -I. -I../daemon
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

lib: clean threadUtil static_analysis $(OBJS) library

build: threadUtil static_analysis $(OBJS) library

threadUtil:
	$(CXX) -c threadUtil.cpp $(CCFLAGS) $(INCLUDES) $(EXTRACCFLAGS) $(LDLIBS) -lpthread -o threadUtil.o
	ar rcs libthreadUtil.a threadUtil.o $(EXTRAARFLAGS)

library:
	ar rcs libcommon.a $(COMMON_OBJS) $(EXTRAARFLAGS)
	ar rcs libipmiUtil.a ipmiUtil.o $(EXTRAARFLAGS)
	ar rcs libpingUtil.a pingUtil.o $(EXTRAARFLAGS)
	ar rcs libnodeBase.a nodeBase.o $(EXTRAARFLAGS)
	ar rcs libregexUtil.a regexUtil.o $(EXTRAARFLAGS)
	ar rcs libhostUtil.a hostUtil.o $(EXTRAARFLAGS)

testhead:
	$(CCFLAGS) $(INCLUDES) -L../daemon -L../common -L. $(LDLIBS) -lpingUtil -lnodeBase -lregexUtil -lhostUtil 

clean:
	@rm -v -f *.o *.a
