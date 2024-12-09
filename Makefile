DEBUG ?= true

CC = gcc
CFLAGS = -std=c11
ifeq ($(DEBUG), true)
	CFLAGS += -Wall -Wextra -Wfatal-errors -g -Og
else
	CFLAGS += -O3
endif
CFLAGS += -c

INCLUDEDIR = include
BUILDDIR = build
SRCDIR = src

WORKLOADS = $(BUILDDIR)/count

all: $(BUILDDIR)/checkpoint $(BUILDDIR)/restore $(WORKLOADS)

$(BUILDDIR)/count: $(BUILDDIR)/count.o
	$(CC) $^ -o $@

$(BUILDDIR)/count.o: $(SRCDIR)/count.c
	$(CC) $(CFLAGS) $^ -o $@

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

.PHONY: all clean