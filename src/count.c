/*
count.c increment a counter in a loop
*/

#include <stdio.h>
#include <unistd.h>

int main() {
  int counter = 0;
  while (1) {
    counter++;
    printf("Counter: %d\n", counter);
    sleep(1);
  }
}