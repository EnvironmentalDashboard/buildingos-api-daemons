CC=gcc
CFLAGS=-g -pedantic -std=c99 -Wall -Wextra
LFLAGS=-lcurl
MYSQL_CONFIG=`mysql_config --cflags --libs`

all: live

live: live.o ./lib/cJSON/cJSON.o
	$(CC) $(CFLAGS) live.o ./lib/cJSON/cJSON.o $(MYSQL_CONFIG) -o live $(LFLAGS)

live.o: live.c
	$(CC) $(CFLAGS) -c live.c $(MYSQL_CONFIG) $(LFLAGS)

clean:
	rm live *.o