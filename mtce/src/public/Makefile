#
# Copyright (c) 2014-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SRCS = amon.c
OBJS = $(SRCS:.c=.o)
INCLUDES = -I.
LDFLAGS = -shared
CFLAGS = -fPIC -g -O2 -Wall -Wextra -Werror
TARGET_LIB = libamon.so
lib: build
ifeq (,$(shell which ${CC}))
CC=gcc
endif

STATIC_ANALYSIS_TOOL = cppcheck
STATIC_ANALYSIS_TOOL_EXISTS = $(shell [[ -e `which $(STATIC_ANALYSIS_TOOL)` ]] && echo 1 || echo 0)

.c.o:
	$(CC) $(INCLUDES) $(CFLAGS) $(EXTRACCFLAGS) -c $< -o $@

static_analysis:
ifeq ($(STATIC_ANALYSIS_TOOL_EXISTS), 1)
	$(STATIC_ANALYSIS_TOOL) --language=c++ --enable=warning -U__AREA__ -DWANT_FIT_TESTING *.cpp *.h
else
	echo "Warning: '$(STATIC_ANALYSIS_TOOL)' static analysis tool not installed ; bypassing ..."
endif

build: static_analysis ${TARGET_LIB}

${TARGET_LIB}: ${TARGET_LIB}.${VER_MJR}
	ln -sf $^ $@

${TARGET_LIB}.${VER_MJR}: ${TARGET_LIB}.${VER}
	ln -sf $^ $@

${TARGET_LIB}.${VER}: $(OBJS)
	$(CC) ${LDFLAGS} -Wl,-soname,${TARGET_LIB}.${VER_MJR} -o $@ $^

$(SRCS:.c=.d):%.d:%.c
	$(CC) $(CFLAGS) -MM $< >$@

include $(SRCS:.c=.d)

clean:
	@rm -v -f *.o
	@rm -v -f *.so
	@rm -v -f *.so.*
	@rm -v -f *.d
