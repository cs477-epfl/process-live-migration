#include <stdio.h>
#include <stdlib.h>

#define KVSIZE 0x40000

int data[KVSIZE];

int get_data_by_key(int key) {
  if (key < 0 || key >= KVSIZE) {
    printf("Invalid key\n");
    return 0;
  }
  printf("Get(%d) = %d\n", key, data[key]);
  return data[key];
}

void put_data_at_key(int key, int value) {
  if (key < 0 || key >= KVSIZE) {
    printf("Invalid key\n");
    return;
  }
  data[key] = value;
  printf("Put(%d, %d)\n", key, value);
}

int main() {
  unsigned long iter = 0;
  const int step = 100000000;
  while (1) {
    if (iter % step != 0) {
      iter++;
      continue;
    }

    int key = iter % KVSIZE;
    int end = key + 200 > KVSIZE ? KVSIZE : key + 200;
    for (int i = key; i < end; i++) {
      put_data_at_key(i, rand());
      get_data_by_key(i);
    }

    iter++;
  }
}