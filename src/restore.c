#include "checkpoint.h"
#include "parse_checkpoint.h"
#include "ptrace.h"
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void inspect_step_by_step(pid_t pid) {
  while (1) {
    // // wait for user input
    // char ch = getchar();
    // if (ch == 'q') {
    //   // continue the child process
    //   if (ptrace(PTRACE_CONT, pid, NULL, NULL) == -1) {
    //     perror("ptrace(PTRACE_CONT)");
    //     return;
    //   }
    //   break;
    // }

    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) == -1) {
      perror("ptrace(PTRACE_GETREGS)");
      return;
    }

    // get all registers
    unsigned long r15 = regs.r15;
    unsigned long r14 = regs.r14;
    unsigned long r13 = regs.r13;
    unsigned long r12 = regs.r12;
    unsigned long rbp = regs.rbp;
    unsigned long rbx = regs.rbx;
    unsigned long r11 = regs.r11;
    unsigned long r10 = regs.r10;
    unsigned long r9 = regs.r9;
    unsigned long r8 = regs.r8;
    unsigned long rax = regs.rax;
    unsigned long rcx = regs.rcx;
    unsigned long rdx = regs.rdx;
    unsigned long rsi = regs.rsi;
    unsigned long rdi = regs.rdi;
    unsigned long orig_rax = regs.orig_rax;
    unsigned long rip = regs.rip;
    unsigned long cs = regs.cs;
    unsigned long eflags = regs.eflags;
    unsigned long rsp = regs.rsp;

    // print all registers
    // printf("RIP: %lx, R15: %lx, R14: %lx, R13: %lx, R12: %lx, RBP: %lx, RBX:
    // "
    //        "%lx, R11: "
    //        "%lx, R10: %lx, R9: %lx, R8: %lx, RAX: %lx, RCX: %lx, RDX: %lx, "
    //        "RSI: %lx, RDI: %lx, ORIG_RAX: %lx, CS: %lx, EFLAGS: %lx, "
    //        "RSP: %lx\n",
    //        rip, r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8, rax, rcx,
    //        rdx, rsi, rdi, orig_rax, cs, eflags, rsp);

    if (ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL) == -1) {
      perror("ptrace(PTRACE_SINGLESTEP)");
      return;
    }
    if (waitpid(pid, NULL, 0) == -1) {
      perror("waitpid");
      return;
    }
  }
}

static void print_mappings(pid_t pid) {
  char maps_path[256];
  snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
  FILE *maps_file = fopen(maps_path, "r");
  if (!maps_file) {
    perror("fopen maps");
    return;
  }
  while (!feof(maps_file)) {
    char line[256];
    if (fgets(line, sizeof(line), maps_file)) {
      printf("%s", line);
    }
  }
  fclose(maps_file);
}

static unsigned long parse_permissions(const char *permissions) {
  unsigned long ret = 0;
  if (permissions[0] == 'r') {
    ret |= PROT_READ;
  }
  if (permissions[1] == 'w') {
    ret |= PROT_WRITE;
  }
  if (permissions[2] == 'x') {
    ret |= PROT_EXEC;
  }
  return ret;
}

int update_mappings(const memory_dump_t *memory_dump, size_t num_regions) {
  for (size_t i = 0; i < num_regions; i++) {
    const memory_region_t *region = &memory_dump->regions[i];
    unsigned long start = region->start;
    unsigned long size = region->size;
    unsigned long offset = region->offset;
    unsigned long permissions = parse_permissions(region->permissions);
    const char *path = region->path;
    const char *content = region->content;

    // skip kernel-related regions
    if (strcmp(path, "[vdso]") == 0 || strcmp(path, "[vsyscall]") == 0 ||
        strcmp(path, "[vvar]") == 0) {
      continue;
    }

    unsigned long flags = MAP_PRIVATE | MAP_FIXED;
    if (strcmp(path, "[stack]") == 0) {
      flags |= MAP_GROWSDOWN;
    }

    if (strlen(path) > 0 && strstr(path, "/")) {
      // file-backed region
      int fd = open(path, O_RDONLY);
      if (fd == -1) {
        perror("open");
        return -1;
      }
      void *addr = mmap((void *)start, size, permissions, flags, fd, offset);
      if (addr == MAP_FAILED) {
        perror("mmap file-backed");
        return -1;
      }
      close(fd);
    } else {
      // anonymous region
      void *addr =
          mmap((void *)start, size, permissions, flags | MAP_ANONYMOUS, -1, 0);
      if (addr == MAP_FAILED) {
        perror("mmap anonymous");
        return -1;
      }
      if (content) {
        memcpy(addr, content, size);
      }
    }
  }
  return 0;
}

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
  struct user *user_dump = &dump.user_dump;
  struct user_regs_struct *regs = &user_dump->regs;

  int child = fork();
  if (child == -1) {
    perror("fork");
    return EXIT_FAILURE;
  }
  if (child == 0) {
    // Child process
    if (update_mappings(memory_dump, memory_dump->num_regions) == -1) {
      return EXIT_FAILURE;
    }

    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1) {
      perror("ptrace(PTRACE_TRACEME)");
      return EXIT_FAILURE;
    }
    raise(SIGSTOP);

    while (1) {
      printf("Child running\n");
      sleep(1);
    }

  } else {
    // Parent process
    int status;
    if (waitpid(child, &status, 0) == -1) {
      perror("waitpid");
      return EXIT_FAILURE;
    }
    if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP) {
      printf("Child stopped by SIGSTOP\n");
    } else if (WIFEXITED(status)) {
      printf("Child exited with status %d\n", WEXITSTATUS(status));
      return EXIT_FAILURE;
    } else {
      fprintf(stderr, "Child stopped by signal %d\n", WSTOPSIG(status));
      return EXIT_FAILURE;
    }

    print_mappings(child);

    // restore user registers
    if (ptrace(PTRACE_SETREGS, child, NULL, regs) == -1) {
      perror("ptrace(PTRACE_SETREGS)");
      return EXIT_FAILURE;
    }

    inspect_step_by_step(child);

    detach_process(child);

    waitpid(child, NULL, NULL);
  }

  return EXIT_SUCCESS;
}