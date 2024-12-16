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
WORKLOADBUILDDIR = build/workload

WORKLOADS = $(WORKLOADBUILDDIR)/count_iter $(WORKLOADBUILDDIR)/count_recur $(WORKLOADBUILDDIR)/matrix_multiplication $(WORKLOADBUILDDIR)/matrix_static

all: $(BUILDDIR)/checkpoint $(BUILDDIR)/restore $(WORKLOADS)

# Pattern rule for building object files
$(WORKLOADBUILDDIR)/%.o: $(WORKLOADDIR)/%.c
	$(WORKLOADCC) $(WORKLOADCFLAGS) $(CFLAGS) -c $< -o $@

# Pattern rule for building executables
$(WORKLOADBUILDDIR)/%: $(WORKLOADBUILDDIR)/%.o
	$(WORKLOADCC) $(WORKLOADCFLAGS) $^ -o $@


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
	mkdir $(WORKLOADBUILDDIR)
	rm -f *.bin
	rm -rf *.log

.PHONY: all clean