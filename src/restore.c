#include "ptrace.h"
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "checkpoint.h"

static int write_map_to_restorer(int restorer_fd, pid_t pid) {
  // read the memory map of the process
  char maps_path[256];
  unsigned long offset;
  char dev[12];
  unsigned long inode;
  snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
  FILE *maps_file = fopen(maps_path, "r");
  if (maps_file == NULL) {
    perror("fopen");
    return -1;
  }

  // write the memory map to the restorer
  while (!feof(maps_file)) {
    memory_region_t region;
    memset(&region, 0, sizeof(region));
    char line[256];
    if (fgets(line, sizeof(line), maps_file) == NULL) {
      break;
    }
    int items_parsed = sscanf(line, "%lx-%lx %4s %lx %11s %lu %255[^\n]",
                              &region.start, &region.end, region.permissions,
                              &offset, dev, &inode, region.path);
    if (items_parsed < 6) {
      printf("Failed to parse line: %s\n", line);
      continue;
    }
    if (items_parsed < 7) {
      strcpy(region.path, "");
    }
    printf("start: %lx, end: %lx, size: %lu, path: %s\n", region.start,
           region.end, region.end - region.start, region.path);

    region.size = region.end - region.start;
    region.content = malloc(region.size);

    int wn = write(restorer_fd, &region, sizeof(region));
    free(region.content);
    if (wn != sizeof(region)) {
      perror("write");
      fclose(maps_file);
      return -1;
    }
  }
  fclose(maps_file);
  return 0;
}

int main(int argc, char **argv) {
  int restorer_fd = open("/dev/restore_memory", O_RDWR);
  if (restorer_fd < 0) {
    perror("open");
    return EXIT_FAILURE;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return EXIT_FAILURE;
  } else if (pid == 0) {
    // Child
    while (1) {
      pause();
    }
  } else {
    // Parent: using ptrace to stop the child, and inspect its memory
    assert(attach_process(pid) == 0);

    assert(write(restorer_fd, &pid, sizeof(pid)) == sizeof(pid));

    if (write_map_to_restorer(restorer_fd, pid) < 0) {
      printf("Failed to write memory map to restorer\n");
      close(restorer_fd);
      return EXIT_FAILURE;
    }

    assert(detach_process(pid) == 0);
    kill(pid, SIGKILL);
  }

  close(restorer_fd);
  return EXIT_SUCCESS;
}