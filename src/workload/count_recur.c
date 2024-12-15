/*
count_recursive.c increment a counter and print recursively
*/

#include <stdio.h>
#include <unistd.h>

#define MAX_RECURSION 100000

void recursive_print_count(unsigned long count) {
  if (count > MAX_RECURSION) {
    return;
  }
  printf("Count: %lu\n", count);
  for (int i = 0; i < 100000; i++) {
    count++;
  }
  count -= 100000;
  recursive_print_count(count + 1);
}

int main() {
  unsigned long long int counter = 0;
  while (1) {
    recursive_print_count(counter);
  }
}
