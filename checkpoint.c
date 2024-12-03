#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/stat.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const char *log_dir = "ckp";            // directory to store the log files
const char *regs_log = "registers.log"; // file to store the register values

// Read the register values from the stopped target process and write them to
// the registers_log file
int save_registers(pid_t pid, struct user_regs_struct *regs) {
  // Retrieve the register values
  if (ptrace(PTRACE_GETREGS, pid, NULL, regs) == -1) {
    perror("ptrace(PTRACE_GETREGS)");
    return -1;
  }

  // Print out register values (for x86_64 architecture)
  printf("Read register values for PID %d:\n", pid);
  printf("\tRIP: %llx\n", regs->rip);
  printf("\tRSP: %llx\n", regs->rsp);
  printf("\tRBP: %llx\n", regs->rbp);
  printf("\tRAX: %llx\n", regs->rax);
  printf("\tRBX: %llx\n", regs->rbx);
  printf("\tRCX: %llx\n", regs->rcx);
  printf("\tRDX: %llx\n", regs->rdx);
  printf("\tRSI: %llx\n", regs->rsi);
  printf("\tRDI: %llx\n", regs->rdi);
  printf("\t... and more\n");

  // patch the register values into the local file
  char *save_path = malloc(strlen(log_dir) + strlen(regs_log) + 2);
  sprintf(save_path, "%s/%s", log_dir, regs_log);
  FILE *f = fopen(save_path, "w");
  if (f == NULL) {
    perror("fopen");
    return -1;
  }
  if (fwrite(regs, sizeof(*regs), 1, f) != 1) {
    perror("fwrite");
    fclose(f);
    return -1;
  }
  fclose(f);
  printf("Register values saved to file: %s\n", regs_log);

  return 0;
}

// Attach to the target process and stop it for further inspection
int attach_process(pid_t pid) {
  // Attach to the target process
  if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1) {
    perror("ptrace(PTRACE_ATTACH)");
    return -1;
  }

  // Wait for the target process to stop
  int status;
  if (waitpid(pid, &status, 0) == -1) {
    perror("waitpid");
    return -1;
  }

  // Check if the process stopped as expected
  if (!WIFSTOPPED(status)) {
    fprintf(stderr, "Process did not stop as expected.\n");
    ptrace(PTRACE_DETACH, pid, NULL, NULL);
    return -1;
  }
  printf("Successfully attached to PID %d\n", pid);

  return 0;
}

// Detach from the target process and stop it.
int detach_process(pid_t pid) {
  if (ptrace(PTRACE_DETACH, pid, NULL, SIGSTOP) == -1) {
    perror("ptrace(PTRACE_DETACH)");
    return -1;
  }
  return 0;
}

// Validate the register values by comparing them with the values saved in the
// file
int validate_register_values(pid_t pid, struct user_regs_struct *regs) {
  char *save_path = malloc(strlen(log_dir) + strlen(regs_log) + 2);
  sprintf(save_path, "%s/%s", log_dir, regs_log);
  FILE *f = fopen(save_path, "r");
  if (f == NULL) {
    perror("fopen");
    return -1;
  }

  struct user_regs_struct regs_restored;
  if (fread(&regs_restored, sizeof(regs_restored), 1, f) != 1) {
    perror("fread");
    fclose(f);
    return -1;
  }
  fclose(f);

  // compare regs and regs_restored
  regs_restored.rax = 0xdeadbeef;
  if (memcmp(regs, &regs_restored, sizeof(regs_restored)) == 0) {
    printf("Validate: restored register values match the original ones\n");
  } else {
    fprintf(stderr, "Error: restored register values do not match "
                    "the original values\n");
    return -1;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
    return EXIT_FAILURE;
  }

  pid_t target_pid = atoi(argv[1]);
  struct user_regs_struct regs;

  if (attach_process(target_pid) == -1) {
    return EXIT_FAILURE;
  }

  // create log directory
  struct stat st = {0};
  if (stat(log_dir, &st) == -1) {
    if (mkdir(log_dir, 0755) == -1) {
      perror("mkdir");
      detach_process(target_pid);
      return EXIT_FAILURE;
    }
  }

  if (save_registers(target_pid, &regs) == -1) {
    detach_process(target_pid);
    return EXIT_FAILURE;
  }

  if (validate_register_values(target_pid, &regs) == -1) {
    detach_process(target_pid);
    return EXIT_FAILURE;
  }

  if (detach_process(target_pid) == -1) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
