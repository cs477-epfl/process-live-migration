#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>

bool print_already = false;

void print_maps(pid_t pid) {
  char path[40];
  snprintf(path, sizeof(path), "/proc/%d/maps", pid);
  FILE *maps = fopen(path, "r");
  if (maps == NULL) {
    perror("fopen");
    return;
  }

  char line[256];
  while (fgets(line, sizeof(line), maps)) {
    printf("%s", line);
  }

  fclose(maps);
}

void print_mem(pid_t pid) {
  char path[40];
  snprintf(path, sizeof(path), "/proc/%d/mem", pid);
  FILE *mem = fopen(path, "r");
  if (mem == NULL) {
    perror("fopen");
    return;
  }

  const unsigned long main_code_start = 0x401745;
  const unsigned long main_code_end = 0x40174b + 5;

  char buf[main_code_end - main_code_start];

  fseek(mem, main_code_start, SEEK_SET);
  fread(buf, sizeof(buf), 1, mem);
  for (int i = 0; i < sizeof(buf); i++) {
    printf("0x%02x ", buf[i]);
  }
  printf("\n");

  fclose(mem);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <program> [args...]\n", argv[0]);
        return 1;
    }

    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        return 1;
    }

    if (child == 0) {
        // Child process
        // Allow tracing of this process
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
            perror("ptrace");
            return 1;
        }

        printf("the binary: %s, the argument: %s \n", argv[1], argv[2]);
        execvp(argv[1], &argv[1]);
        // If execvp returns, there was an error
        perror("execvp");
        return 1;
    } else {
        // Parent process
        int status;

        while (1) {
            // Wait for the child to stop on a syscall entry or exit
            waitpid(child, &status, 0);
            printf("wait from parent\n");

            if (WIFEXITED(status)) {
                // Child has exited
                printf("Child exited with status %d\n", WEXITSTATUS(status));
                break;
            }

            printf("parent: child is stopped\n");
            // Child stopped on a syscall exit
            struct user_regs_struct regs;
            if (ptrace(PTRACE_GETREGS, child, NULL, &regs) == -1) {
              perror("ptrace");
              return 1;
            }
            printf("pc addr %lx \n", regs.rip);
            // Print syscall number and return value
            printf("Syscall %lld returned %lld\n", regs.orig_rax, regs.rax);

            if (regs.orig_rax == 230 && !print_already) {
              print_maps(child);
              print_mem(child);
              print_already = true;
            }

            // Continue the child and stop at the next syscall
            if (ptrace(PTRACE_SYSCALL, child, NULL, NULL) == -1) {
              perror("ptrace");
              return 1;
            }
        }
    }

    return 0;
}

