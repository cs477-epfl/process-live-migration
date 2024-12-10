#include "ptrace.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "checkpoint.h"

static int construct_dump_struct(process_dump_t *dump) {
  char line[256];
  const int max_regions = 30;

  dump->num_regions = 0;
  dump->regions = malloc(sizeof(memory_region_t) * max_regions);

  // read /proc/pid/maps file to construct dump.regions
  pid_t pid = getpid();
  char maps_path[256];
  snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
  FILE *maps_file = fopen(maps_path, "r");
  if (maps_file == NULL) {
    perror("fopen");
    return -1;
  }

  while (fgets(line, sizeof(line), maps_file)) {
    memory_region_t region;
    unsigned long offset;
    char dev[12];
    unsigned long inode;

    int items_parsed = sscanf(line, "%lx-%lx %4s %lx %11s %lu %255[^\n]",
                              &region.start, &region.end, region.permissions,
                              &offset, dev, &inode, region.path);
    if (items_parsed < 6) {
      fprintf(stderr, "Failed to parse line: %s\n", line);
      continue;
    }
    if (items_parsed < 7) {
      strcpy(region.path, "");
    }

    region.size = region.end - region.start;
    region.content = malloc(region.size);
    // region.content == NULL;

    printf("start: %lx, end: %lx, permissions: %s, path: %s, size: %zu\n",
           region.start, region.end, region.permissions, region.path,
           region.size);
    dump->regions[dump->num_regions++] = region;

    if (dump->num_regions >= max_regions) {
      break;
    }
  }
  fclose(maps_file);
  return 0;
}

int main(int argc, char **argv) {
  int restorer_fd = open("/dev/restore_process", O_RDWR);
  if (restorer_fd < 0) {
    perror("open");
    return EXIT_FAILURE;
  }

  process_dump_t dump;
  if (construct_dump_struct(&dump) < 0) {
    return EXIT_FAILURE;
  }

  // write dump to /dev/restore_memory
  if (write(restorer_fd, &dump, sizeof(dump)) < 0) {
    perror("write");
    return EXIT_FAILURE;
  }

  close(restorer_fd);

  return EXIT_SUCCESS;
}