CFLAGS=-Wall -g -std=c++11 -O2

OS_NAME=$(shell uname)
StaticLib=libjsoncpp.a

ifeq ($(OS_NAME), Linux)
DynamicLib=libjsoncpp.so
CFLAGS := $(CFLAGS) -fPIC
LDFLAGS=-shared
else
DynamicLib=libjsoncpp.dylib
LDFLAGS=-dynamiclib
endif

#源文件目录
SrcDir= .
#头文件目录
IncDir= .
#连接库目录
LibDir=

SRCS=$(foreach dir,$(SrcDir),$(wildcard $(dir)/*.cpp))
INCS=$(foreach dir,$(IncDir),$(addprefix -I,$(dir)))
LINKS=$(foreach dir,$(LibDir),$(addprefix -L,$(dir)))
CFLAGS := $(CFLAGS) $(INCS)
LDFLAGS:= $(LINKS) $(LDFLAGS)

OBJS = $(SRCS:%.cpp=%.o)

.PHONY:all clean

all: $(StaticLib) $(DynamicLib)

$(StaticLib):$(OBJS)
	ar -r $(StaticLib) $(OBJS)

$(DynamicLib): $(OBJS)
	g++ -o $(DynamicLib) $(OBJS) $(LDFLAGS)

%.o:%.cpp
	g++ $(CFLAGS) -c $< -o $@

clean:
	rm -fr $(OBJS) $(StaticLib) $(DynamicLib) 
