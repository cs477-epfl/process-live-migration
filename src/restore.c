#include "checkpoint.h"
#include "parse_checkpoint.h"
#include "ptrace.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <dump_file>\n", argv[0]);
    return EXIT_FAILURE;
  }

  process_dump_t dump;

  const char *dump_file = argv[1];
  memset(&dump, 0, sizeof(dump));

  if (load_process_dump(dump_file, &dump) == -1) {
    return EXIT_FAILURE;
  }

  int restorer_fd = open("/dev/restore_process", O_RDWR);
  if (restorer_fd < 0) {
    perror("open");
    return EXIT_FAILURE;
  }

  // write dump to /dev/restore_memory
  if (write(restorer_fd, &dump, sizeof(dump)) < 0) {
    perror("write");
    return EXIT_FAILURE;
  }

  close(restorer_fd);
  printf("dump written to /dev/restore_process\n");

  return EXIT_SUCCESS;
}