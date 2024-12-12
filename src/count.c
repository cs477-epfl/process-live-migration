/*
count.c increment a counter in a loop
*/

#include <stdio.h>
#include <unistd.h>

int main() {
  unsigned long long int counter = 0;
  while (1) {
    counter++;
  }
}
