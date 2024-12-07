#include "checkpoint.h"

void free_process_dump(process_dump_t *dump);

int load_process_dump(const char *filename, process_dump_t *dump);