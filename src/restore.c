#include "checkpoint.h"
#include "parse_checkpoint.h"
#include "ptrace.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "util.h"

/// `write` has syscall ID 1.
#ifndef __NR_write
#define __NR_write 1
#endif

int main(int argc, char **argv) {
  if (argc != 2) {
    FPRINTF(stderr, "Usage: %s <dump_file>\n", argv[0]);
    return EXIT_FAILURE;
  }

  // Parse dump file
  process_dump_t dump;
  const char *dump_file = argv[1];
  memset(&dump, 0, sizeof(dump));
  if (unlikely(load_process_dump(dump_file, &dump) == -1)) {
    return EXIT_FAILURE;
  }

  pid_t child = fork();
  if (unlikely(child == -1)) {
    perror("fork");
    return 1;
  } else if (child == 0) { // Child process
    if (unlikely(ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1)) {
      perror("ptrace");
      return 1;
    }
    raise(SIGSTOP); // Parent waits for `SIGSTOP` before tracing syscall.
    
    // Open virtual device
    int restorer_fd = open("/dev/restore_process", O_RDWR);
    if (unlikely(restorer_fd < 0)) {
      perror("open");
      return EXIT_FAILURE;
    }
    // Restore memory using virtual device
    if (unlikely(write(restorer_fd, &dump, sizeof(dump)) < 0)) {
      perror("write");
      return EXIT_FAILURE;
    }

    close(restorer_fd);
    PRINTF("dump written to /dev/restore_process\n");
  } else { // Parent process
    int status;
    struct user_regs_struct regs;

    bool written = false;
    while (1) {
      waitpid(child, &status, 0);
      PRINTF("parent waits\n");

      if (WIFEXITED(status)) {
        PRINTF("child exited with status %d\n", WEXITSTATUS(status));
        break;
      }
      
      if (unlikely(ptrace(PTRACE_GETREGS, child, NULL, &regs) == -1)) {
        perror("ptrace");
        return 1;
      }

      PRINTF("parent: child calls syscall %lld\n", regs.orig_rax);

      // Check if this is a write syscall return
      if (!written && regs.orig_rax == __NR_write) {
        written = true;
        PRINTF("in `write` syscall\n");
      } else if (regs.orig_rax == __NR_write) {
        PRINTF("`write` syscall returned: %llx\n", regs.rax);
        break;
      }

      if (unlikely(ptrace(PTRACE_SYSCALL, child, NULL, NULL) == -1)) {
        perror("ptrace");
        return 1;
      }
    }
    PRINTF("parent: child returns from `write` syscall\n");
  }

  return EXIT_SUCCESS;
}