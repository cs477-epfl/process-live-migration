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

  // Save registers
  if (read_registers(target_pid, &dump.regs) == -1) {
    detach_process(target_pid);
    return EXIT_FAILURE;
  }

  // save mm_info
  if (read_mm_info(target_pid, &dump.mm_info) == -1) {
    detach_process(target_pid);
    return EXIT_FAILURE;
  }

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

int read_registers(pid_t pid, struct user_regs_struct *regs) {
  // Retrieve the register values
  if (ptrace(PTRACE_GETREGS, pid, NULL, regs) == -1) {
    perror("ptrace(PTRACE_GETREGS)");
    return -1;
  }

  printf("Registers retrieved for PID %d\n", pid);
  return 0;
}


int read_mm_info(pid_t pid, mm_info_t *mm_info) {
  char stat_path[256];
  snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);

  FILE *stat_file = fopen(stat_path, "r");
  if (!stat_file) {
    perror("fopen stat");
    return -1;
  }

  char buffer[8192];
  if (!fgets(buffer, sizeof(buffer), stat_file)) {
    perror("fgets stat");
    fclose(stat_file);
    return -1;
  }
  fclose(stat_file);

  //printf("content: %s\n", buffer);

  // Variables to hold parsed values
  int pid_in_stat;
  char comm[256];
  char state;
  unsigned long long field_values[52]; // Use unsigned long long for large values
  memset(field_values, 0, sizeof(field_values));

  // Parse pid, comm, and state
  // The comm field can contain spaces and is enclosed in parentheses
  // We need to locate the opening and closing parentheses
  char *ptr = buffer;
  char *start_comm = strchr(ptr, '(');
  char *end_comm = strrchr(ptr, ')');

  if (!start_comm || !end_comm || start_comm >= end_comm) {
    fprintf(stderr, "Failed to parse comm field in stat file\n");
    return -1;
  }

  // Extract pid
  *start_comm = '\0'; // Temporarily terminate the string to isolate pid
  if (sscanf(ptr, "%d", &pid_in_stat) != 1) {
    fprintf(stderr, "Failed to parse pid from stat file\n");
    return -1;
  }
  *start_comm = '('; // Restore the '(' character

  // Extract comm
  size_t comm_len = end_comm - start_comm - 1;
  strncpy(comm, start_comm + 1, comm_len);
  comm[comm_len] = '\0';

  // Move ptr to after the closing parenthesis
  ptr = end_comm + 1;

  // Skip spaces
  while (*ptr == ' ' || *ptr == '\t') ptr++;

  // Extract state
  if (sscanf(ptr, "%c", &state) != 1) {
    fprintf(stderr, "Failed to parse state from stat file\n");
    return -1;
  }

  // Move ptr past the state character
  ptr++;

  // Now parse the rest of the fields
  int field_index = 4;
  while (field_index < 52 && *ptr != '\0') {
    // Skip spaces
    while (*ptr == ' ' || *ptr == '\t') ptr++;

    if (*ptr == '\0') {
      break;
    }

    char *endptr;
    errno = 0;
    unsigned long long value = strtoull(ptr, &endptr, 10);
    // print the characters between ptr and endptr
    //printf("field %d: %.*s (0x%llx)\n", field_index, (int)(endptr - ptr), ptr, value);
    //printf("field %d: %llu\n", field_index + 4, value);
    if (errno != 0 || ptr == endptr) {
      fprintf(stderr, "Failed to parse field %d\n", field_index);
      return -1;
    }

    field_values[field_index] = value;
    ptr = endptr;
    field_index++;
  }

  if (field_index < 52) {
    fprintf(stderr, "Expected at least 52 fields, but got %d\n", field_index + 3); // +3 for pid, comm, state
    return -1;
  }

  // Assign the values to mm_info
  mm_info->start_code  = (unsigned long)field_values[26]; // Field 25
  mm_info->end_code    = (unsigned long)field_values[27]; // Field 26
  mm_info->start_stack = (unsigned long)field_values[28]; // Field 28
  mm_info->start_data  = (unsigned long)field_values[45]; // Field 45
  mm_info->end_data    = (unsigned long)field_values[46]; // Field 46
  mm_info->start_brk   = (unsigned long)field_values[47]; // Field 47

  // Debug prints to verify values
  printf("Parsed mm_struct info for PID %d:\n", pid_in_stat);
  printf("start_code: 0x%lx\n", mm_info->start_code);
  printf("end_code:   0x%lx\n", mm_info->end_code);
  printf("start_data: 0x%lx\n", mm_info->start_data);
  printf("end_data:   0x%lx\n", mm_info->end_data);
  printf("start_brk:  0x%lx\n", mm_info->start_brk);
  printf("brk:        0x%lx\n", mm_info->brk);
  printf("start_stack: 0x%lx\n", mm_info->start_stack);

  return 0;
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
    return -1;
  }

  ssize_t bytes_read =
      pread(mem_fd, region->content, region->size, region->start);
  if (bytes_read != (ssize_t)region->size) {
    // It's common that some memory regions cannot be read entirely
    // due to permissions, so we handle partial reads or errors gracefully
    perror("pread");
    free(region->content);
    close(mem_fd);
    return -1;
  }
  close(mem_fd);
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
  size_t regions_capacity = 10;
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

    // Parse the line
    // Format: start_addr-end_addr perms offset dev inode pathname
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

    // Check if the region should be saved, if yes, read the content from mem
    // file
    if (should_save_region(&region)) {
      get_memory_area(&region, mem_path);
      printf("save memory region [%zu]: %s\n", dump->num_regions, line);
    } else {
      region.size = 0;
      printf("skip memory region [%zu]: %s\n", dump->num_regions, line);
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

  // Write the register values
  if (fwrite(&dump->regs, sizeof(dump->regs), 1, file) != 1) {
    perror("fwrite regs");
    fclose(file);
    return -1;
  }

  // write mm_info
  if (fwrite(&dump->mm_info, sizeof(dump->mm_info), 1, file) != 1) {
    perror("fwrite mm_info");
    fclose(file);
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
        fwrite(region->permissions, sizeof(region->permissions), 1, file) !=
            1 ||
        fwrite(region->path, sizeof(region->path), 1, file) != 1) {
      perror("fwrite region metadata");
      fclose(file);
      return -1;
    }

    // Write the memory content
    if (region->size == 0) {
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