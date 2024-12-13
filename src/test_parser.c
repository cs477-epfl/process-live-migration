#include "../include/parse_checkpoint.h"

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <dump_file>\n", argv[0]);
    return -1;
  }

  const char *filename = argv[1];
  process_dump_t dump;

  if (load_process_dump(filename, &dump) != 0) {
    fprintf(stderr, "Failed to load process dump\n");
    return -1;
  }

  // Print important fields from the user struct
  printf("User struct details:\n");
  printf("RIP: 0x%llx\n", dump.user_dump.regs.rip);
  printf("RSP: 0x%llx\n", dump.user_dump.regs.rsp);
  printf("RAX: 0x%llx\n", dump.user_dump.regs.rax);
  printf("RBX: 0x%llx\n", dump.user_dump.regs.rbx);
  printf("RCX: 0x%llx\n", dump.user_dump.regs.rcx);
  printf("RDX: 0x%llx\n", dump.user_dump.regs.rdx);
  printf("Command: %s\n", dump.user_dump.u_comm);

  // print other user struct info
  printf("start_code: 0x%llx\n", dump.user_dump.start_code);
  printf("start_stack: 0x%llx\n", dump.user_dump.start_stack);
  printf("tsize: %llu\n", dump.user_dump.u_tsize);
  printf("dsize: %llu\n", dump.user_dump.u_dsize);
  printf("ssize: %llu\n", dump.user_dump.u_ssize);

  printf("Number of memory regions: %zu\n", dump.memory_dump.num_regions);
  for (size_t i = 0; i < dump.memory_dump.num_regions; i++) {
    memory_region_t *region = &dump.memory_dump.regions[i];
    printf("Region %zu: %lx-%lx (%s) %s (offset = %lu), size: %zu\n", i,
           region->start, region->end, region->permissions, region->path,
           region->offset, region->size);
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

  // Free the loaded memory dump
  free_process_dump(&dump);

  return 0;
}