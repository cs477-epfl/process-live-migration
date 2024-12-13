#include "checkpoint.h"
#include "parse_checkpoint.h"
#include "ptrace.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
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

  memory_dump_t *memory_dump = &dump.memory_dump;

  int child = fork();
  if (child == -1) {
    perror("fork");
    return EXIT_FAILURE;
  }
  if (child == 0) {
    // Child process
    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
      perror("ptrace(PTRACE_TRACEME)");
      return EXIT_FAILURE;
    }
    raise(SIGSTOP);

    int restorer_fd = open("/dev/krestore_mapping", O_WRONLY);
    if (restorer_fd == -1) {
      perror("open restorer_fd");
      return EXIT_FAILURE;
    }

    if (write(restorer_fd, memory_dump, sizeof(memory_dump_t)) == -1) {
      perror("write num_regions");
      return EXIT_FAILURE;
    }

    // should not reach here
    assert(0);
  } else {
    // Parent process

    int status;
    if (waitpid(child, &status, 0) == -1) {
      perror("waitpid");
      return EXIT_FAILURE;
    }
    printf("Child stopped\n");

    while (1) {
      // inspect syscall
      if (ptrace(PTRACE_SYSCALL, child, NULL, NULL) == -1) {
        perror("ptrace(PTRACE_SYSCALL)");
        return EXIT_FAILURE;
      }
      if (waitpid(child, NULL, 0) == -1) {
        perror("waitpid");
        return EXIT_FAILURE;
      }

      // get the syscall number
      struct user_regs_struct regs;
      if (ptrace(PTRACE_GETREGS, child, NULL, &regs) == -1) {
        perror("ptrace(PTRACE_GETREGS)");
        return EXIT_FAILURE;
      }
      long syscall = regs.orig_rax;
      printf("syscall: %ld\n", syscall);

      // check if the syscall is exit
      if (syscall == 60) {
        break;
      }

      // continue the process
      if (ptrace(PTRACE_SYSCALL, child, NULL, NULL) == -1) {
        perror("ptrace(PTRACE_SYSCALL)");
        return EXIT_FAILURE;
      }
      if (waitpid(child, NULL, 0) == -1) {
        perror("waitpid");
        return EXIT_FAILURE;
      }
      // read syscall and return value
      if (ptrace(PTRACE_GETREGS, child, NULL, &regs) == -1) {
        perror("ptrace(PTRACE_GETREGS)");
        return EXIT_FAILURE;
      }
      syscall = regs.orig_rax;
      int ret = regs.rax;
      printf("syscall exit: %ld, ret = %d\n", syscall, ret);

      if (syscall == 1) {
        // write syscall returns from the kernel module

        // TODO: poke user and poke registers to child process
        break;
      }
    }

    // print memory mappings of child process
    char maps_path[256];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", child);
    FILE *maps_file = fopen(maps_path, "r");
    if (!maps_file) {
      perror("fopen maps");
      return -1;
    }
    while (!feof(maps_file)) {
      char line[256];
      if (fgets(line, sizeof(line), maps_file)) {
        printf("%s", line);
      }
    }
    fclose(maps_file);

    // TODO: not kill the child process
    if (kill(child, SIGKILL) == -1) {
      perror("kill");
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}