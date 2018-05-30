#
# Copyright (c) 2014 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SRCS = rmon_api_client_test.cpp
OBJS = $(SRCS:.cpp=.o)
LDLIBS = -lstdc++ -lrmonapi
INCLUDES = -I../ -I.  
CCFLAGS = -g -O2 -Wall -Wextra -Werror

all: build

.cpp.o:
	$(CXX) $(INCLUDES) $(CCFLAGS) $(EXTRACCFLAGS) -c $< -o $@

build: $(OBJS)
	$(CXX) $(CCFLAGS) $(OBJS) -L../ -L/usr/lib64/ $(LDLIBS) -o rmond_api_test

clean: 
	@rm -f $(OBJ) rmond_api_test *.o *.a
