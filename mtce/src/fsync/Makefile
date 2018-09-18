#
# Copyright (c) 2015-2018 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SRCS = fsync.c
OBJS = $(SRCS:.c=.o)
LDLIBS = -L.
INCLUDES = -I.
CCFLAGS = -g -O2 -Wall -Wextra -Werror

all: build

.c.o:
	$(CC) $(INCLUDES) $(CCFLAGS) $(EXTRACCFLAGS) -c $< -o $@

build: $(OBJS)
	$(CC) $(OBJS) $(LDLIBS) -o fsync

clean: 
	@rm -v -f $(OBJS) fsync

