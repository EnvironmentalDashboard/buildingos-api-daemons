CC=gcc
# https://stackoverflow.com/questions/1778538/how-many-gcc-optimization-levels-are-there
# -ggdb3 is for valgrind, -g option is for gdb debugger; remove in production
CFLAGS=-Og -pedantic -std=c99 -Wall -Wextra -ggdb3 -std=gnu11
LFLAGS=-lcurl
MYSQL_CONFIG=`mysql_config --cflags --libs`

all: buildingosd

buildingosd: buildingosd.o ./lib/cJSON/cJSON.o
	$(CC) $(CFLAGS) buildingosd.o ./lib/cJSON/cJSON.o $(MYSQL_CONFIG) -o buildingosd $(LFLAGS) -fPIC

buildingosd.o: buildingosd.c
	$(CC) $(CFLAGS) -c buildingosd.c $(MYSQL_CONFIG) $(LFLAGS)

clean:
	rm buildingosd *.o