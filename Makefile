CC = gcc
CFLAGS = -Wall -g

WORKLOADS = count
LOGS = ckp/*.log

all: checkpoint $(WORKLOADS)

count: count.o
	$(CC) $(CFLAGS) -o count count.o

count.o: count.c
	$(CC) $(CFLAGS) -c count.c

checkpoint: checkpoint.o
	$(CC) $(CFLAGS) -o checkpoint checkpoint.o

checkpoint.o: checkpoint.c
	$(CC) $(CFLAGS) -c checkpoint.c

clean:
	rm -f count count.o checkpoint checkpoint.o $(LOGS)

.PHONY: all clean