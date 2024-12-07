#include <fcntl.h>
#include <stdio.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

// Function to attach to the target process
int attach_process(pid_t pid);

// Function to detach from the target process
int detach_process(pid_t pid);
