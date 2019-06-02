CC=g++

ver=release
CFLAGS=-Wall -g -std=c++11
ifeq ($(ver), release)
CFLAGS += -O2
else
CFLAGS += -DDEBUG
endif

ifdef version
CFLAGS+=-DREDIS_PORT_VERSION=\"$(version)\"
endif

LDFLAGS= -lbase -lpthread ../3rd_party/hiredis/libhiredis.a 

# link static stdc++ library, so no need to install this library on the running OS
OS=$(shell uname -s)
ifeq ($(OS), Linux)
LDFLAGS += -static-libstdc++
endif

RM=/bin/rm -rf
ARCH=PC

# target binary object
BIN=redis_port

SrcDir= . ../3rd_party ../redis_parser
IncDir= . ../base ../3rd_party ../3rd_party/hiredis ../redis_parser
LibDir= ../base/

SRCS=$(foreach dir,$(SrcDir),$(wildcard $(dir)/*.cpp))
INCS=$(foreach dir,$(IncDir),$(addprefix -I,$(dir)))
LINKS=$(foreach dir,$(LibDir),$(addprefix -L,$(dir)))
CFLAGS := $(CFLAGS) $(INCS)
LDFLAGS:= $(LINKS) $(LDFLAGS)

OBJS = $(SRCS:%.cpp=%.o)
.PHONY:all clean

all:$(BIN)
$(BIN):$(OBJS) ../base/libbase.a ../3rd_party/hiredis/libhiredis.a
	$(CC) -o $(BIN) $(OBJS) $(LDFLAGS)
	@echo " OK!\tCompile $@ "

../3rd_party/hiredis/libhiredis.a:
	make -C ../3rd_party/hiredis

%.o:%.cpp
	@echo "$(CC) $(CFLAGS) -c $< -o $@"
	@$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	@echo "[$(ARCH)] \tCleaning files..."
	@$(RM) $(OBJS) $(BIN)
