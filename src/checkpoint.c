#include "checkpoint.h"
#include "ptrace.h"

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <pid> <output_file>\n", argv[0]);
    return EXIT_FAILURE;
  }

  pid_t target_pid = atoi(argv[1]);
  const char *output_file = argv[2];

  // Check if the target process exists
  if (kill(target_pid, 0) == -1 && errno != EPERM) {
    perror("kill");
    return EXIT_FAILURE;
  }

  if (attach_process(target_pid) == -1) {
    return EXIT_FAILURE;
  }

  process_dump_t dump;
  memset(&dump, 0, sizeof(dump));

  // Read memory regions
  if (read_memory_regions(target_pid, &dump) == -1) {
    detach_process(target_pid);
    free_process_dump(&dump);
    return EXIT_FAILURE;
  }

  if (detach_process(target_pid) == -1) {
    free_process_dump(&dump);
    return EXIT_FAILURE;
  }

  // Save the dump to a file
  if (save_process_dump(output_file, &dump) == -1) {
    free_process_dump(&dump);
    return EXIT_FAILURE;
  }

  // Free allocated memory
  free_process_dump(&dump);

  return EXIT_SUCCESS;
}

bool should_save_region(const memory_region_t *region) {
  // Skip special regions
  if (strstr(region->path, "[vdso]") || strstr(region->path, "[vvar]") ||
      strstr(region->path, "[vsyscall]"))
    return false;

  // Skip device mappings
  if (strstr(region->path, "/dev/"))
    return false;

  return true;
}

int get_memory_area(memory_region_t *region, const char *mem_path) {
  // Read the memory content
  region->content = malloc(region->size);
  if (!region->content) {
    perror("malloc region.content");
    return -1;
  }

  int mem_fd = open(mem_path, O_RDONLY);
  if (mem_fd == -1) {
    perror("open mem");
    free(region->content);
    region->content = NULL;
    return -1;
  }

  ssize_t bytes_read =
      pread(mem_fd, region->content, region->size, region->start);
  if (bytes_read != (ssize_t)region->size) {
    // It's common that some memory regions cannot be read entirely
    // due to permissions, so we handle partial reads or errors gracefully
    perror("pread");
    free(region->content);
    region->content = NULL;
    close(mem_fd);
    return -1;
  }
  close(mem_fd);
  return 0;
}

int read_memory_region(const char *line, memory_region_t *region,
                       const char *mem_path) {
  // Format: start_addr-end_addr perms offset dev inode pathname
  char dev[12];
  unsigned long inode;
  int items_parsed = sscanf(line, "%lx-%lx %4s %lx %11s %lu %255[^\n]",
                            &region->start, &region->end, region->permissions,
                            &region->offset, dev, &inode, region->path);
  if (items_parsed < 6) {
    fprintf(stderr, "Failed to parse line: %s\n", line);
    return -1;
  }
  if (items_parsed < 7) {
    strcpy(region->path, "");
  }

  region->size = region->end - region->start;

  if (should_save_region(region)) {
    if (strlen(region->path) > 0 && strstr(region->path, "/") != NULL) {
      // if file-backed, don't save the content
      region->content = NULL;
    } else {
      // anonymous memory, read the content
      if (get_memory_area(region, mem_path) < 0) {
        return -1;
      }
    }
    printf("%lx-%lx %s %s (offset: %lx) size: %zu\n", region->start,
           region->end, region->permissions, region->path, region->offset,
           region->size);
  } else {
    printf("skip content: %s\n", line);
  }

  return 0;
}

int read_memory_regions(pid_t pid, process_dump_t *dump) {
  char maps_path[256], mem_path[256];
  snprintf(
      maps_path, sizeof(maps_path), "/proc/%d/maps",
      pid); // See https://man7.org/linux/man-pages/man5/proc_pid_maps.5.html
  snprintf(mem_path, sizeof(mem_path), "/proc/%d/mem", pid);

  FILE *maps_file = fopen(maps_path, "r");
  if (!maps_file) {
    perror("fopen maps");
    return -1;
  }

  // Initialize memory regions array
  size_t regions_capacity = 20;
  dump->regions = malloc(regions_capacity * sizeof(memory_region_t));
  if (!dump->regions) {
    perror("malloc");
    fclose(maps_file);
    return -1;
  }
  dump->num_regions = 0;

  char line[512];
  while (fgets(line, sizeof(line), maps_file)) {
    memory_region_t region;
    memset(&region, 0, sizeof(region));

    if (read_memory_region(line, &region, mem_path) < 0) {
      fclose(maps_file);
      return -1;
    }

    // Add the region to the dump
    if (dump->num_regions >= regions_capacity) {
      regions_capacity *= 2;
      memory_region_t *new_regions =
          realloc(dump->regions, regions_capacity * sizeof(memory_region_t));
      if (!new_regions) {
        perror("realloc");
        free(region.content);
        break;
      }
      dump->regions = new_regions;
    }
    dump->regions[dump->num_regions++] = region;
  }

  fclose(maps_file);
  return 0;
}

int save_process_dump(const char *filename, process_dump_t *dump) {
  FILE *file = fopen(filename, "wb");
  if (!file) {
    perror("fopen dump file");
    return -1;
  }

  // Write the number of memory regions
  if (fwrite(&dump->num_regions, sizeof(dump->num_regions), 1, file) != 1) {
    perror("fwrite num_regions");
    fclose(file);
    return -1;
  }

  // Write each memory region
  for (size_t i = 0; i < dump->num_regions; i++) {
    memory_region_t *region = &dump->regions[i];

    // Write the memory region metadata
    if (fwrite(&region->start, sizeof(region->start), 1, file) != 1 ||
        fwrite(&region->end, sizeof(region->end), 1, file) != 1 ||
        fwrite(&region->size, sizeof(region->size), 1, file) != 1 ||
        fwrite(&region->offset, sizeof(region->offset), 1, file) != 1 ||
        fwrite(region->permissions, sizeof(region->permissions), 1, file) !=
            1 ||
        fwrite(region->path, sizeof(region->path), 1, file) != 1) {
      perror("fwrite region metadata");
      fclose(file);
      return -1;
    }

    // Write the memory content
    if (region->size == 0 || region->content == NULL) {
      continue;
    }
    if (fwrite(region->content, 1, region->size, file) != region->size) {
      perror("fwrite region content");
      fclose(file);
      return -1;
    }
  }

  fclose(file);
  printf("Process dump saved to %s\n", filename);
  return 0;
}

void free_process_dump(process_dump_t *dump) {
  for (size_t i = 0; i < dump->num_regions; i++) {
    free(dump->regions[i].content);
  }
  free(dump->regions);
}
