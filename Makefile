
CC = gcc
AR = ar
CFLAGS = -Wall -O0 -std=gnu99 -I/usr/local/include
LDFLAGS = -L/usr/local/lib -lzmq
DEBUG ?= -g -rdynamic -ggdb

OBJ = blowfish.o sds.o zmalloc.o sdsutils.o slre.o 
PROGS = zmqcat zmqsend

all : $(PROGS)

# Deps (from 'make dep')
blowfish.o: blowfish.c blowfish.h
sds.o: sds.c sds.h zmalloc.h
sdsutils.o: sdsutils.c sdsutils.h sds.h slre.h blowfish.h zmalloc.h
slre.o: slre.c slre.h
zmalloc.o: zmalloc.c config.h
zmqcat.o: zmqcat.c
zmqsend.o: zmqsend.c

# Targets
zmqcat : zmqcat.o $(OBJ)
	$(CC) -o zmqcat $(LDFLAGS) $(DEBUG) zmqcat.o $(OBJ)

zmqsend : zmqsend.o $(OBJ)
	$(CC) -o zmqsend $(LDFLAGS) $(DEBUG) zmqsend.o $(OBJ)

# Generic build targets
.c.o:
	$(CC) -c $(CFLAGS) $(DEBUG) $<

dep:
	$(CC) -MM *.c

clean:
	rm -rf $(PROGS) $(LIB) *.o *~

