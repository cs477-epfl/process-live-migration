#include "krestore.h"

#define x86
#define DEVICE_NAME "restore_process"
#define CLASS_NAME "virtual"

static int major_number;
static struct class *device_class = NULL;
static struct device *device_device = NULL;

static struct file_operations fops = {
    .open = device_open,
    .read = device_read,
    .write = device_write,
    .release = device_release,
};

enum dev_state {
  ENTRY,     // initial state, device is free
  REMAPPING, // waiting for following writes to get memory mapping and contents
};

static struct krestore_args_t {
  enum dev_state state;
  pid_t pid; // which process we are restoring memory on
} krestore_config;

// Implement your file operations here
static int device_open(struct inode *inodep, struct file *filep) {
  printk(KERN_INFO "/dev/krestore: Device has been opened\n");
  if (krestore_config.state != ENTRY) {
    return -EBUSY;
  }
  krestore_config.state = REMAPPING;
  printk(KERN_INFO "/dev/krestore: Device is ready -> REMAPPING\n");
  return 0;
}

static int device_release(struct inode *inodep, struct file *filep) {
  krestore_config.state = ENTRY;
  krestore_config.pid = 0;
  printk(KERN_INFO "/dev/krestore: Device has been closed -> ENTRY\n");
  return 0;
}

static ssize_t device_read(struct file *filep, char *buffer, size_t len,
                           loff_t *offset) {
  // read is not supported
  return -EINVAL;
}

static ssize_t device_write(struct file *filep, const char *buffer, size_t len,
                            loff_t *offset) {
  switch (krestore_config.state) {

  case REMAPPING:
    process_dump_t dump;
    int ret_parse = parse_dump_from_user(&dump, buffer, len);
    if (ret_parse != 0) {
      return ret_parse;
    }
    print_memory_regions(dump.regions, dump.num_regions);

    int ret = 0;
    ret = unmap_all();
    if (ret != 0) {
      goto free_return;
    }
    flush_tlb_cache();
    
    ret = map_all(dump.regions, dump.num_regions);
    if (ret != 0) {
      goto free_return;
    }
    flush_tlb_cache();

  free_return:
    free_process_dump(&dump);
    return ret;

  default:
    return -EINVAL;
  }
}

static void print_memory_regions(const memory_region_t *regions, size_t num) {
  printk(KERN_INFO "Number of regions: %zu\n", num);
  size_t i = 0;
  for (; i < num; i++) {
    printk(KERN_INFO "/dev/krestore: Region %zu: %lx-%lx %s %s %zu\n", i,
           regions[i].start, regions[i].end, regions[i].permissions,
           regions[i].path, regions[i].size);
  }
}

static int unmap_all(void) {
  struct mm_struct *mm = current->mm;
  struct vm_area_struct *vma = mm->mmap;
  int ret = 0;

  // mmap_write_lock(mm);
  while (vma) {
    struct vm_area_struct *next_vma = vma->vm_next;
    if (vma->vm_start == mm->context.vdso || (vma->vm_flags & VM_SPECIAL)) {
      vma = next_vma;
      continue;
    }

    ret = vm_munmap(vma->vm_start, vma->vm_end - vma->vm_start);
    if (ret != 0) {
      break;
    }
    vma = next_vma;
  }
  // mmap_write_unlock(mm);

  return ret;
}

// Flush TLB entries and cache lines for current process
static void flush_tlb_cache(void) {
  struct mm_struct *mm = current->mm;
  if (mm) {
    flush_tlb_mm(mm);
  }
  mb(); // Full Linux memory barrier
#ifdef x86
  wbinvd(); // Write back and invalidate all cache lines
  asm volatile("mfence" ::: "memory"); // x86 memory barrier
#else
  #error "Unsupported architecture"
#endif
  printk(KERN_DEBUG "/dev/krestore: Flushed TLB and cache lines\n");
}

static unsigned long parse_permissions(const char *permissions) {
  unsigned long ret = 0;
  if (permissions[0] == 'r') {
    ret |= PROT_READ;
  }
  if (permissions[1] == 'w') {
    ret |= PROT_WRITE;
  }
  if (permissions[2] == 'x') {
    ret |= PROT_EXEC;
  }
  return ret;
}

static int map_all(const memory_region_t *regions, size_t num) {
  size_t ptr = 0; // pointer to the current region
  int ret = 0;
  for (; ptr < num; ptr++) {
    memory_region_t *region = &regions[ptr];
    unsigned long start = region->start;
    unsigned long size = region->size;
    unsigned long permissions = parse_permissions(region->permissions);
    const char *path = region->path;
    // skip kernel-related regions
    if (strcmp(path, "[vdso]") == 0 || strcmp(path, "[vsyscall]") == 0 ||
        strcmp(path, "[vvar]") == 0) {
      continue;
    }

    const char *content = region->content;
    unsigned long flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
    if (strcmp(path, "[stack]") == 0) {
      flags |= MAP_GROWSDOWN;
    }

    ret = vm_mmap(NULL, start, size, permissions, flags, 0);
    if (IS_ERR_VALUE(ret)) {
      printk(KERN_ALERT "/dev/krestore: Failed to mmap region %lx-%lx, %s\n",
             start, start + size, path);
      break;
    }

    // if (content != NULL) {
    //   ret = copy_to_user((void *)start, content, size);
    //   if (ret != 0) {
    //     break;
    //   }
    // }
  }

  return ret;
}

static int parse_dump_from_user(process_dump_t *dump, const char *buffer,
                                size_t len) {
  if (len != sizeof(process_dump_t)) {
    return -EINVAL;
  }

  // shallow copy of the dump struct: regs, mm_info, num_regions
  process_dump_t dump_tmp;
  if (copy_from_user(&dump_tmp, buffer, len) != 0) {
    return -EFAULT;
  }

  // copy of the regions: start, end, permissions, path, size
  memory_region_t *user_regions = dump_tmp.regions;
  dump_tmp.regions =
      kmalloc(dump_tmp.num_regions * sizeof(memory_region_t), GFP_KERNEL);
  if (copy_from_user(dump_tmp.regions, user_regions,
                     dump_tmp.num_regions * sizeof(memory_region_t)) != 0) {
    kfree(dump_tmp.regions);
    return -EFAULT;
  }

  // deep copy: copy the contents of each region if not null
  size_t i = 0;
  for (; i < dump_tmp.num_regions; i++) {
    if (dump_tmp.regions[i].content != NULL) {
      char *user_content = dump_tmp.regions[i].content;
      dump_tmp.regions[i].content =
          kmalloc(dump_tmp.regions[i].size, GFP_KERNEL);
      if (dump_tmp.regions[i].content == NULL) {
        size_t j = 0;
        for (; j < i; j++) {
          kfree(dump_tmp.regions[j].content);
        }
        kfree(dump_tmp.regions);
        return -ENOMEM;
      }

      if (copy_from_user(dump_tmp.regions[i].content, user_content,
                         dump_tmp.regions[i].size) != 0) {
        size_t j = 0;
        for (; j < i; j++) {
          kfree(dump_tmp.regions[j].content);
        }
        kfree(dump_tmp.regions[i].content);
        kfree(dump_tmp.regions);
        return -EFAULT;
      }
    }
  }

  *dump = dump_tmp; // copy back to the original dump

  return 0;
}

static void free_process_dump(process_dump_t *dump) {
  size_t i = 0;
  for (; i < dump->num_regions; i++) {
    if (dump->regions[i].content != NULL) {
      kfree(dump->regions[i].content);
    }
  }
  kfree(dump->regions);
}

static int __init virtual_device_init(void) {
  // Register the device with a major number
  major_number = register_chrdev(0, DEVICE_NAME, &fops);
  if (major_number < 0) {
    printk(KERN_ALERT "Failed to register a major number\n");
    return major_number;
  }

  // Register the device class
  device_class = class_create(THIS_MODULE, CLASS_NAME);
  if (IS_ERR(device_class)) {
    unregister_chrdev(major_number, DEVICE_NAME);
    return PTR_ERR(device_class);
  }

  // Register the device driver
  device_device = device_create(device_class, NULL, MKDEV(major_number, 0),
                                NULL, DEVICE_NAME);
  if (IS_ERR(device_device)) {
    class_destroy(device_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    return PTR_ERR(device_device);
  }

  krestore_config.state = ENTRY;

  return 0;
}

static void __exit virtual_device_exit(void) {
  device_destroy(device_class, MKDEV(major_number, 0));
  class_unregister(device_class);
  class_destroy(device_class);
  unregister_chrdev(major_number, DEVICE_NAME);
}

module_init(virtual_device_init);
module_exit(virtual_device_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yuchen Ouyang");
MODULE_DESCRIPTION("A virtual device driver supporting memory restore");