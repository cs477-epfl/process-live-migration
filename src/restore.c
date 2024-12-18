#include "checkpoint.h"
#include "ptrace.h"
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static long long get_time_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  long long current_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  return current_time;
}

int recv_dump(process_dump_t *dump, int socket_fd) {
  // Read the user struct
  if (recv(socket_fd, &dump->user_dump, sizeof(struct user), 0) == -1) {
    perror("recv user_dump");
    return -1;
  }

  // Read the number of memory regions
  if (recv(socket_fd, &dump->memory_dump.num_regions, sizeof(size_t), 0) ==
      -1) {
    perror("recv num_regions");
    return -1;
  }

  // Allocate memory for the memory regions
  dump->memory_dump.regions =
      malloc(dump->memory_dump.num_regions * sizeof(memory_region_t));
  if (!dump->memory_dump.regions) {
    perror("malloc regions");
    return -1;
  }

  // Read each memory region
  for (size_t i = 0; i < dump->memory_dump.num_regions; i++) {
    memory_region_t *region = &dump->memory_dump.regions[i];

    // Read the memory region metadata
    if (recv(socket_fd, &region->start, sizeof(region->start), 0) == -1 ||
        recv(socket_fd, &region->end, sizeof(region->end), 0) == -1 ||
        recv(socket_fd, &region->size, sizeof(region->size), 0) == -1 ||
        recv(socket_fd, &region->offset, sizeof(region->offset), 0) == -1 ||
        recv(socket_fd, region->permissions, sizeof(region->permissions), 0) ==
            -1 ||
        recv(socket_fd, region->path, sizeof(region->path), 0) == -1) {
      perror("recv region metadata");
      return -1;
    }

    // Read the memory content
    if (region->size > 0 &&
        !(strlen(region->path) > 0 && strstr(region->path, "/")) &&
        !(strstr(region->path, "[vvar]") ||
          strstr(region->path, "[vsyscall]") ||
          strstr(region->path, "[vdso]"))) {
      region->content = malloc(region->size);
      if (!region->content) {
        perror("malloc region content");
        return -1;
      }

      // TCP will send in multiple chunks
      size_t received = 0;
      while (received < region->size) {
        ssize_t ret = recv(socket_fd, region->content + received,
                           region->size - received, 0);
        if (ret == -1) {
          perror("recv region content");
          return -1;
        }
        received += ret;
      }

    } else {
      region->content = NULL;
    }

    printf("Recv Region %zu: %lx-%lx (%s) %s (offset=%lx), size: %zu\n", i,
           region->start, region->end, region->permissions, region->path,
           region->offset, region->size);
  }

  return 0;
}

void inspect_step_by_step(pid_t pid) {
  while (1) {
    // wait for user input
    char ch = getchar();
    if (ch == 'q') {
      // continue the child process
      if (ptrace(PTRACE_CONT, pid, NULL, NULL) == -1) {
        perror("ptrace(PTRACE_CONT)");
        return;
      }
      break;
    }

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
    printf("RIP: %lx, R15: %lx, R14: %lx, R13: %lx, R12: %lx, RBP: %lx, RBX:"
           "%lx, R11: "
           "%lx, R10: %lx, R9: %lx, R8: %lx, RAX: %lx, RCX: %lx, RDX: %lx, "
           "RSI: %lx, RDI: %lx, ORIG_RAX: %lx, CS: %lx, EFLAGS: %lx, "
           "RSP: %lx\n",
           rip, r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8, rax, rcx, rdx,
           rsi, rdi, orig_rax, cs, eflags, rsp);

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

int tracer(pid_t child, bool step_by_step,
           const struct user_regs_struct *regs_dump) {
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

  while (1) {
    // inspect syscall entry
    if (ptrace(PTRACE_SYSCALL, child, NULL, NULL) == -1) {
      perror("ptrace(PTRACE_SYSCALL)");
      return EXIT_FAILURE;
    }
    if (waitpid(child, &status, 0) == -1) {
      perror("waitpid");
      return EXIT_FAILURE;
    }

    // inspect syscall exit
    if (ptrace(PTRACE_SYSCALL, child, NULL, NULL) == -1) {
      perror("ptrace(PTRACE_SYSCALL)");
      return EXIT_FAILURE;
    }
    if (waitpid(child, &status, 0) == -1) {
      perror("waitpid");
      return EXIT_FAILURE;
    }

    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, child, NULL, &regs) == -1) {
      perror("ptrace(PTRACE_GETREGS)");
      return EXIT_FAILURE;
    }
    unsigned long orig_rax = regs.orig_rax;
    if (orig_rax == SYS_write) {
      break;
    }
  }

  print_mappings(child);

  // restore user registers
  if (ptrace(PTRACE_SETREGS, child, NULL, regs_dump) == -1) {
    perror("ptrace(PTRACE_SETREGS)");
    return EXIT_FAILURE;
  }

  if (step_by_step) {
    inspect_step_by_step(child);
  }

  detach_process(child);

  long long end_time = get_time_ms();
  printf("migration end time: %lld ms\n", end_time);
  return EXIT_SUCCESS;
}

int tracee(const memory_dump_t *memory_dump) {
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

  if (write(restorer_fd, memory_dump, sizeof(*memory_dump)) == -1) {
    perror("write memory_dump");
    return EXIT_FAILURE;
  }

  assert(0); // should not reach here
}

int main(int argc, char **argv) {
  // Usage: ./restore <listen port> [-f <file path>] [-s]
  int opt;
  char *log_filename = NULL;
  int log_fd = -1;
  bool step_by_step = false;
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <listen port> [-f <file path>] [-s]\n", argv[0]);
    return EXIT_FAILURE;
  }
  const char *listen_port = argv[1];
  while (opt = getopt(argc, argv, "f:s"), opt != -1) {
    switch (opt) {
    case 'f':
      log_filename = optarg;
      break;
    case 's':
      step_by_step = true;
      break;
    default:
      fprintf(stderr, "Usage: %s <listen port> [-f <file path>] [-s]\n",
              argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (log_filename) {
    log_fd = open(log_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd == -1) {
      perror("open log file");
      return EXIT_FAILURE;
    }
  }

  process_dump_t dump;
  memset(&dump, 0, sizeof(dump));

  const char *listen_host = "127.0.0.1";
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(atoi(listen_port));
  addr.sin_addr.s_addr = inet_addr(listen_host);

  // create a listen socket at the specified port
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }
  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind");
    return EXIT_FAILURE;
  }
  if (listen(listen_fd, 1) == -1) {
    perror("listen");
    return EXIT_FAILURE;
  }
  printf("Listening on %s:%s\n", listen_host, listen_port);

  // accept a connection from the client and print everything
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);
  int socket_fd =
      accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
  if (socket_fd == -1) {
    perror("accept");
    return EXIT_FAILURE;
  }

  if (recv_dump(&dump, socket_fd) == -1) {
    printf("Failed to load dump from client\n");
    return EXIT_FAILURE;
  }

  memory_dump_t *memory_dump = &dump.memory_dump;
  struct user *user_dump = &dump.user_dump;
  struct user_regs_struct *regs_dump = &user_dump->regs;

  int child = fork();
  if (child == -1) {
    perror("fork");
    return EXIT_FAILURE;
  }
  if (child == 0) {
    int ret = tracee(memory_dump);
    if (ret == EXIT_FAILURE) {
      return EXIT_FAILURE;
    }
  } else {
    // make log_fd the standard output
    if (log_fd != -1) {
      dup2(log_fd, STDOUT_FILENO);
    }

    int ret = tracer(child, step_by_step, regs_dump);
    if (ret == EXIT_FAILURE) {
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}