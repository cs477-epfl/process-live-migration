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

// a struct to hold the essential fields in mm_struct
typedef struct {
  unsigned long start_code;
  unsigned long end_code;
  unsigned long start_data;
  unsigned long end_data;
  unsigned long start_brk;
  unsigned long brk;
  unsigned long start_stack;
} mm_info_t;

// Define a structure to hold the entire process state
typedef struct {
  struct user_regs_struct regs;
  mm_info_t mm_info;
  size_t num_regions;
  memory_region_t *regions;
} process_dump_t;

// Function to check if a memory region should be saved
// We skip device mappings, vsyscall, vvar, vdso.
bool should_save_region(const memory_region_t *region);

// Function to read one memory region from /proc/<pid>/mem and save it to
// region.content
int get_memory_area(memory_region_t *region, const char *mem_path);

// Function to read memory regions from /proc/<pid>/maps and /proc/<pid>/mem
int read_memory_regions(pid_t pid, process_dump_t *dump);

// Function to read essential fields from mm_struct
int read_mm_info(pid_t pid, mm_info_t *mm_info);

// Function to read the register values of a process
int read_registers(pid_t pid, struct user_regs_struct *regs);

// Function to save the process dump to a file
int save_process_dump(const char *filename, process_dump_t *dump);

// Function to free the memory allocated in the dump
void free_process_dump(process_dump_t *dump);

#endif