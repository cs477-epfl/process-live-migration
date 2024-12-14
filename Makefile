DEBUG ?= true

CC = gcc
CFLAGS = -std=c11
MUSL = /usr/local/musl/bin/musl-gcc
ifeq ($(DEBUG), true)
	CFLAGS += -Wall -Wextra -Wfatal-errors -g -Og -std=gnu11
else
	CFLAGS += -O3 -std=gnu11
endif
CFLAGS += -c
WORKLOADCFLAGS = -static -no-pie -fno-pie -fno-pic -fno-plt -fcf-protection=none

INCLUDEDIR = include
BUILDDIR = build
SRCDIR = src

WORKLOADS = $(BUILDDIR)/count

all: $(BUILDDIR)/checkpoint $(BUILDDIR)/restore $(BUILDDIR)/test_parser $(WORKLOADS)

$(BUILDDIR)/count: $(BUILDDIR)/count.o
	$(MUSL) $(WORKLOADCFLAGS) $^ -o $@

$(BUILDDIR)/count.o: $(SRCDIR)/count.c
	$(MUSL) $(WORKLOADCFLAGS) $(CFLAGS) $^ -o $@

$(BUILDDIR)/checkpoint: $(BUILDDIR)/checkpoint.o $(BUILDDIR)/ptrace.o
	$(CC) $^ -o $@

$(BUILDDIR)/test_parser: $(BUILDDIR)/test_parser.o $(BUILDDIR)/parse_checkpoint.o
	$(CC) $^ -o $@

$(BUILDDIR)/test_parser.o: $(SRCDIR)/test_parser.c
	$(CC) -I$(INCLUDEDIR) $(CFLAGS) $^ -o $@

$(BUILDDIR)/checkpoint.o: $(SRCDIR)/checkpoint.c
	$(CC) -I$(INCLUDEDIR) $(CFLAGS) $^ -o $@

$(BUILDDIR)/ptrace.o: $(SRCDIR)/ptrace.c
	$(CC) -I$(INCLUDEDIR) $(CFLAGS) $^ -o $@

$(BUILDDIR)/parse_checkpoint.o: $(SRCDIR)/parse_checkpoint.c
	$(CC) -I$(INCLUDEDIR) $(CFLAGS) $^ -o $@

$(BUILDDIR)/restore: $(BUILDDIR)/restore.o $(BUILDDIR)/ptrace.o $(BUILDDIR)/parse_checkpoint.o
	$(CC) $^ -o $@

$(BUILDDIR)/restore.o: $(SRCDIR)/restore.c
	$(CC) -I$(INCLUDEDIR) $(CFLAGS) $^ -o $@

clean:
	rm -rf $(BUILDDIR)
	mkdir $(BUILDDIR)
	rm -f *.bin
	rm -rf *.log

.PHONY: all clean