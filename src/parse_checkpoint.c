#include "parse_checkpoint.h"

void free_process_dump(process_dump_t *dump) {
  for (size_t i = 0; i < dump->memory_dump.num_regions; i++) {
    free(dump->memory_dump.regions[i].content);
  }
  free(dump->memory_dump.regions);
}

int load_process_dump(const char *filename, process_dump_t *dump) {
  FILE *file = fopen(filename, "rb");
  if (!file) {
    perror("fopen dump file");
    return -1;
  }

  // Read the user struct
  if (fread(&dump->user_dump, sizeof(struct user), 1, file) != 1) {
    perror("fread user_dump");
    fclose(file);
    return -1;
  }

  // Read the number of memory regions
  if (fread(&dump->memory_dump.num_regions, sizeof(size_t), 1, file) != 1) {
    perror("fread num_regions");
    fclose(file);
    return -1;
  }

  // Allocate memory for the regions
  dump->memory_dump.regions = malloc(dump->memory_dump.num_regions * sizeof(memory_region_t));
  if (!dump->memory_dump.regions) {
    perror("malloc regions");
    fclose(file);
    return -1;
  }



  // Read each memory region
  for (size_t i = 0; i < dump->memory_dump.num_regions; i++) {
    memory_region_t *region = &dump->memory_dump.regions[i];

    // Read the memory region metadata
    if (fread(&region->start, sizeof(region->start), 1, file) != 1 ||
        fread(&region->end, sizeof(region->end), 1, file) != 1 ||
        fread(&region->size, sizeof(region->size), 1, file) != 1 ||
        fread(&region->offset, sizeof(region->offset), 1, file) != 1 ||
        fread(region->permissions, sizeof(region->permissions), 1, file) != 1 ||
        fread(region->path, sizeof(region->path), 1, file) != 1) {
      perror("fread region metadata");
      fclose(file);
      free_process_dump(dump);
      return -1;
    }

    printf("Region %zu: %lx-%lx (%s) %s (offset=%lx), size: %zu\n", i,
           region->start, region->end, region->permissions, region->path,
           region->offset, region->size);

    // Read the memory content when it is anonymous
    if (region->size > 0 &&
        !(strlen(region->path) > 0 && strstr(region->path, "/")) &&
        !(strstr(region->path, "[vvar]") ||
          strstr(region->path, "[vsyscall]") ||
          strstr(region->path, "[vdso]"))) {
      region->content = malloc(region->size);
      if (!region->content) {
        perror("malloc region content");
        fclose(file);
        free_process_dump(dump);
        return -1;
      }
      if (fread(region->content, 1, region->size, file) != region->size) {
        perror("fread region content");
        fclose(file);
        free_process_dump(dump);
        return -1;
      }
    } else {
      region->content = NULL;
    }
  }

  fclose(file);
  printf("Process dump loaded from %s\n", filename);
  return 0;
}

// int main(int argc, char *argv[]) {
//   if (argc != 2) {
//     fprintf(stderr, "Usage: %s <dump_file>\n", argv[0]);
//     return EXIT_FAILURE;
//   }

//   const char *dump_file = argv[1];
//   process_dump_t dump;
//   memset(&dump, 0, sizeof(dump));

//   if (load_process_dump(dump_file, &dump) == -1) {
//     return EXIT_FAILURE;
//   }

//   // At this point, 'dump' contains the loaded process state.
//   // You can access registers via 'dump.regs' and memory regions via
//   // 'dump.regions'. For example, to print out the memory regions:
//   printf("Registers loaded.\n");
//   printf("Number of memory regions: %zu\n", dump.num_regions);
//   for (size_t i = 0; i < dump.num_regions; i++) {
//     memory_region_t *region = &dump.regions[i];
//     printf("Region %zu: %lx-%lx (%s) %s (offset=%lx), size: %zu\n", i,
//            region->start, region->end, region->permissions, region->path,
//            region->offset, region->size);
//     // Optionally, do something with 'region->content'
//   }

//   // Remember to free the allocated memory
//   free_process_dump(&dump);
//   return EXIT_SUCCESS;
// }