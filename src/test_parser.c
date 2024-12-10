#include "../include/parse_checkpoint.h"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <dump_file>\n", argv[0]);
    return EXIT_FAILURE;
  }

  const char *dump_file = argv[1];
  process_dump_t dump;
  memset(&dump, 0, sizeof(dump));

  if (load_process_dump(dump_file, &dump) == -1) {
    return EXIT_FAILURE;
  }

  // At this point, 'dump' contains the loaded process state.
  // You can access registers via 'dump.regs' and memory regions via
  // 'dump.regions'. For example, to print out the memory regions:
  printf("Registers loaded.\n");
  //print the memory info
  mm_info_t *mm_info = &dump.mm_info;
  printf("start_code: 0x%lx\n", mm_info->start_code);
  printf("end_code:   0x%lx\n", mm_info->end_code);
  printf("start_data: 0x%lx\n", mm_info->start_data);
  printf("end_data:   0x%lx\n", mm_info->end_data);
  printf("start_brk:  0x%lx\n", mm_info->start_brk);
  printf("brk:        0x%lx\n", mm_info->brk);
  printf("start_stack: 0x%lx\n", mm_info->start_stack);
  printf("Number of memory regions: %zu\n", dump.num_regions);
  for (size_t i = 0; i < dump.num_regions; i++) {
    memory_region_t *region = &dump.regions[i];
    printf("Region %zu: %lx-%lx (%s) %s, size: %zu\n", i, region->start,
           region->end, region->permissions, region->path, region->size);
    // Optionally, do something with 'region->content'
    if (region->content) {
      printf("Content: ");
      for (size_t j = 0; j < region->size && j < 3; j++) {
        printf("%02x ", (unsigned char)region->content[j]);
      }
      printf("\n");
    } else {
      printf("Content: (null)\n");
    }
  }
  // Remember to free the allocated memory
  free_process_dump(&dump);
  return EXIT_SUCCESS;
}