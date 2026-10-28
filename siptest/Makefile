CC	= gcc
CFLAGS	= -O2 -g -Wall -fcommon -D_GNU_SOURCE -I. \
	-DVERSION_MAJOR=1 -DVERSION_MINOR=0 -DGIT_HASH='"nogit"' \
	-DUNAME_S='"Linux"' -DUNAME_M='"x86_64"' -DSYSTEM_TIMESTAMP=0
LDFLAGS	= -static

OBJS	= esipserv.o esip.o tcp.o utils.o cson.o cobj.o jsonparser.o version.o

all:	esipserv

esipserv:	$(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lpthread -lm

clean:
	rm -f $(OBJS) esipserv
