DEBUG ?= true

CC = gcc
CFLAGS = -std=c11
ifeq ($(DEBUG), true)
	CFLAGS += -Wall -Wextra -Wfatal-errors -g -Og -std=gnu11
else
	CFLAGS += -O3 -std=gnu11
endif
CFLAGS += -c

WORKLOADCC = /usr/local/musl/bin/musl-gcc
WORKLOADCFLAGS = -static -no-pie -fno-pie -fno-pic -fno-plt -fcf-protection=none

INCLUDEDIR = include
BUILDDIR = build
SRCDIR = src
WORKLOADDIR = src/workload

WORKLOADS = $(BUILDDIR)/count_iter $(BUILDDIR)/count_recur

all: $(BUILDDIR)/checkpoint $(BUILDDIR)/restore $(WORKLOADS)

$(BUILDDIR)/count_iter: $(BUILDDIR)/count_iter.o
	$(WORKLOADCC) $(WORKLOADCFLAGS) $^ -o $@

$(BUILDDIR)/count_iter.o: $(WORKLOADDIR)/count_iter.c
	$(WORKLOADCC) $(WORKLOADCFLAGS) $(CFLAGS) $^ -o $@

$(BUILDDIR)/count_recur: $(BUILDDIR)/count_recur.o
	$(WORKLOADCC) $(WORKLOADCFLAGS) $^ -o $@

$(BUILDDIR)/count_recur.o: $(WORKLOADDIR)/count_recur.c
	$(WORKLOADCC) $(WORKLOADCFLAGS) $(CFLAGS) $^ -o $@

$(BUILDDIR)/checkpoint: $(BUILDDIR)/checkpoint.o $(BUILDDIR)/ptrace.o
	$(CC) $^ -o $@

$(BUILDDIR)/checkpoint.o: $(SRCDIR)/checkpoint.c
	$(CC) -I$(INCLUDEDIR) $(CFLAGS) $^ -o $@

$(BUILDDIR)/ptrace.o: $(SRCDIR)/ptrace.c
	$(CC) -I$(INCLUDEDIR) $(CFLAGS) $^ -o $@

$(BUILDDIR)/restore: $(BUILDDIR)/restore.o $(BUILDDIR)/ptrace.o
	$(CC) $^ -o $@

$(BUILDDIR)/restore.o: $(SRCDIR)/restore.c
	$(CC) -I$(INCLUDEDIR) $(CFLAGS) $^ -o $@

clean:
	rm -rf $(BUILDDIR)
	mkdir $(BUILDDIR)
	rm -f *.bin
	rm -rf *.log

.PHONY: all clean