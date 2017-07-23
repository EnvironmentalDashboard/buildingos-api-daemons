CC=gcc
CFLAGS=-g -pedantic -std=c99 -Wall -Wextra
MYSQL_CONFIG=`mysql_config --cflags --libs`

all: live

live: live.o ./lib/cJSON/cJSON.o
	$(CC) $(CFLAGS) live.o ./lib/cJSON/cJSON.o $(MYSQL_CONFIG) -o live

live.o: live.c
	$(CC) $(CFLAGS) -c live.c $(MYSQL_CONFIG)

clean:
	rm live *.o