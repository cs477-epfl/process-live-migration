#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Define a structure to hold memory region information
typedef struct {
  unsigned long start;
  unsigned long end;
  char permissions[5]; // e.g., "rwxp"
  char path[256];      // Path or descriptor (e.g., "[heap]")
  unsigned long offset;
  size_t size;
  char *content;
} memory_region_t;

typedef struct {
  size_t num_regions;
  memory_region_t *regions;
} memory_dump_t;

// Define a structure to hold the entire process state
typedef struct {
  struct user user_dump;
  memory_dump_t memory_dump;
} process_dump_t;

// Function to check if a memory region should be saved
// We skip device mappings, vsyscall, vvar, vdso.
bool should_save_region(const memory_region_t *region);

// Function to read one memory region from /proc/<pid>/mem and save it to
// region.content
int get_memory_area(memory_region_t *region, const char *mem_path);

// Function to read memory regions from /proc/<pid>/maps and /proc/<pid>/mem
int read_memory_regions(pid_t pid, memory_dump_t *dump);

int read_user_info(pid_t pid, struct user *user_dump);

// Function to free the memory allocated in the dump
void free_process_dump(process_dump_t *dump);

#endif