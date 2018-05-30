#
# Copyright (c) 2015-2016 Wind River Systems, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

SRCS =	guestClass.cpp guestInstClass.cpp \
		guestSvrFsm.cpp guestSvrHdlr.cpp \
		guestServer.cpp guestAgent.cpp \
		guestHttpSvr.cpp guestHttpUtil.cpp guestVimApi.cpp \
		guestUtil.cpp  guestSvrUtil.cpp guestSvrMsg.cpp \
		guestVirtio.cpp guestStubs.cpp
AGENT_OBJS = \
		guestAgent.o  guestClass.o guestHttpSvr.o guestHttpUtil.o guestVimApi.o guestUtil.o guestStubs.o
SERVER_OBJS = \
		guestServer.o guestInstClass.o \
		guestSvrFsm.o guestSvrHdlr.o \
		guestSvrMsg.o guestVirtio.o \
		guestUtil.o guestSvrUtil.o

OBJS = $(SRCS:.cpp=.o)

CCPFLAGS = -g -O2 -Wall -Wextra -Werror
LDLIBS = -lstdc++ -ldaemon -lcommon -lfmcommon -ljson-c -levent -lrt -lcrypto -luuid 
INCLUDES = -I../daemon -I../common -I../maintenance -I. -I..

#CFLAGS = -fPIC -g -O2 -Wall -Wextra -Werror -I../daemon -I../common -I. -I..

#.cpp.o:
#	$(CXX) $(INCLUDES) $(CCPFLAGS) $(EXTRACCFLAGS) -c $< -o $@

build: $(OBJS)
	$(CXX) $(CCPFLAGS) $(AGENT_OBJS) -L../daemon -L../common -L../public -L.. $(LDLIBS) -o guestAgent
	$(CXX) $(CCPFLAGS) $(SERVER_OBJS) -L../libs -L../daemon -L../common -L../public -L.. $(LDLIBS) -L. -o guestServer

#$(CXX) $(CCPFLAGS) $(SERVER_OBJS) -L../libs -L../daemon -L../common -L../public -L.. $(LDLIBS) -L. -ltrap_handler -lalarm_clock -latomic -o guestServer

.cpp.o:
	$(CXX) $(INCLUDES) $(CCPFLAGS) $(EXTRACCFLAGS) -c $< -o $@

common:
	( cd ../common ; make clean ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

daemon:
	( cd ../daemon ; make clean ; make lib VER=$(VER) VER_MJR=$(VER_MJR))

clean:
	@rm -f $(OBJ) guestAgent guestServer *.o *.a libghbb.so.1
