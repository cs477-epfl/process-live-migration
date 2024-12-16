#include "checkpoint.h"
#include "ptrace.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>

static long long get_time_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  long long current_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  return current_time;
}

int send_dump(process_dump_t *dump, int socket_fd) {
  size_t total_send_bytes = 0;
  // Send the user struct
  if (send(socket_fd, &dump->user_dump, sizeof(struct user), 0) == -1) {
    perror("send user_dump");
    return -1;
  }
  total_send_bytes += sizeof(struct user);

  // Send the number of memory regions
  if (send(socket_fd, &dump->memory_dump.num_regions, sizeof(size_t), 0) ==
      -1) {
    perror("send num_regions");
    return -1;
  }
  total_send_bytes += sizeof(size_t);

  // Send each memory region
  for (size_t i = 0; i < dump->memory_dump.num_regions; i++) {
    memory_region_t *region = &dump->memory_dump.regions[i];

    // Send the memory region metadata
    if (send(socket_fd, &region->start, sizeof(region->start), 0) == -1 ||
        send(socket_fd, &region->end, sizeof(region->end), 0) == -1 ||
        send(socket_fd, &region->size, sizeof(region->size), 0) == -1 ||
        send(socket_fd, &region->offset, sizeof(region->offset), 0) == -1 ||
        send(socket_fd, region->permissions, sizeof(region->permissions), 0) ==
            -1 ||
        send(socket_fd, region->path, sizeof(region->path), 0) == -1) {
      perror("send region metadata");
      return -1;
    }
    total_send_bytes += sizeof(region->start) + sizeof(region->end) +
                        sizeof(region->size) + sizeof(region->offset) +
                        sizeof(region->permissions) + sizeof(region->path);

    // Send the memory content
    if (region->size > 0 && region->content) {
      if (send(socket_fd, region->content, region->size, 0) == -1) {
        perror("send region content");
        return -1;
      }
      total_send_bytes += region->size;
    }
  }

  printf("Dump sent: %zu bytes\n", total_send_bytes);

  return 0;
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
  }
  return 0;
}

int read_memory_regions(pid_t pid, memory_dump_t *dump) {
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

int read_user_info(pid_t pid, struct user *user_dump) {
  // Calculate the size of the user struct
  size_t user_struct_size = sizeof(struct user);
  long data;
  size_t i;
  // Read the user struct word by word into the user_data struct
  for (i = 0; i < user_struct_size / sizeof(long); i++) {
    errno = 0;
    data = ptrace(PTRACE_PEEKUSER, pid, sizeof(long) * i, NULL);
    if (data == -1) {
      perror("ptrace(PTRACE_PEEKUSER) failed");
      return -1;
    }
    // Copy the data into the user_data struct
    ((long *)user_dump)[i] = data;
  }
  return 0;
}

void free_process_dump(process_dump_t *dump) {
  for (size_t i = 0; i < dump->memory_dump.num_regions; i++) {
    free(dump->memory_dump.regions[i].content);
  }
  free(dump->memory_dump.regions);
}

int main(int argc, char *argv[]) {
  int ret = 0;
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <pid> <ip:port>\n", argv[0]);
    return EXIT_FAILURE;
  }

  // Check if the target process exists
  pid_t target_pid = atoi(argv[1]);
  if (kill(target_pid, 0) == -1 && errno != EPERM) {
    perror("kill");
    return EXIT_FAILURE;
  }

  // parse ip and port to socket address
  char *send_socket = argv[2];
  const char *ip = strtok(send_socket, ":");
  const char *port = strtok(NULL, ":");
  if (ip == NULL || port == NULL) {
    fprintf(stderr, "Invalid ip:port\n");
    return EXIT_FAILURE;
  }
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(atoi(port));
  server_addr.sin_addr.s_addr = inet_addr(ip);
  if (inet_pton(AF_INET, ip, &server_addr.sin_addr) != 1) {
    perror("inet_pton");
    return EXIT_FAILURE;
  }
  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }
  if (connect(socket_fd, (struct sockaddr *)&server_addr,
              sizeof(server_addr)) == -1) {
    perror("connect");
    return EXIT_FAILURE;
  }

  if (attach_process(target_pid) == -1) {
    return EXIT_FAILURE;
  }
  long long start_time = get_time_ms();
  printf("migration start time: %lld ms\n", start_time);

  process_dump_t dump;
  memset(&dump, 0, sizeof(dump));

  // Read memory regions
  if (read_memory_regions(target_pid, &dump.memory_dump) == -1) {
    ret = -1;
    goto ret;
  }

  // get user registers
  if (ptrace(PTRACE_GETREGS, target_pid, NULL, &dump.user_dump.regs) == -1) {
    perror("ptrace(PTRACE_GETREGS)");
    ret = -1;
    goto ret;
  }

  // Send the dump to the server
  if (send_dump(&dump, socket_fd) == -1) {
    ret = -1;
    goto ret;
  }

  // kill the pid
  if (kill(target_pid, SIGKILL) == -1) {
    perror("kill");
    ret = -1;
    goto ret;
  }

ret:
  free_process_dump(&dump);
  return ret;
}