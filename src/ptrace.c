#include "ptrace.h"

int attach_process(pid_t pid) {
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

int detach_process(pid_t pid) {
  if (ptrace(PTRACE_DETACH, pid, NULL, NULL) == -1) {
    perror("ptrace(PTRACE_DETACH)");
    return -1;
  }
  printf("Detached from PID %d\n", pid);
  return 0;
}