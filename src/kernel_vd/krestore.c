#include "krestore.h"

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

static struct restore_memory_args_t {
  enum dev_state state;
  pid_t pid; // which process we are restoring memory on
} restore_memory_config;

// Implement your file operations here
static int device_open(struct inode *inodep, struct file *filep) {
  printk(KERN_INFO "/dev/restore_memory: Device has been opened\n");
  if (restore_memory_config.state != ENTRY) {
    return -EBUSY;
  }
  restore_memory_config.state = REMAPPING;
  printk(KERN_INFO "/dev/restore_memory: Device is ready -> REMAPPING\n");
  return 0;
}

static int device_release(struct inode *inodep, struct file *filep) {
  restore_memory_config.state = ENTRY;
  restore_memory_config.pid = 0;
  printk(KERN_INFO "/dev/restore_memory: Device has been closed -> ENTRY\n");
  return 0;
}

static ssize_t device_read(struct file *filep, char *buffer, size_t len,
                           loff_t *offset) {
  // read is not supported
  return -EINVAL;
}

static ssize_t device_write(struct file *filep, const char *buffer, size_t len,
                            loff_t *offset) {
  switch (restore_memory_config.state) {

  case REMAPPING:
    process_dump_t dump;
    int ret_parse = parse_dump_from_user(&dump, buffer, len);
    if (ret_parse != 0) {
      return ret_parse;
    }
    int ret_unmap = unmap_all();
    if (ret_unmap != 0) {
      return ret_unmap;
    }
    print_memory_regions(dump.regions, dump.num_regions);
    return 0;

  default:
    return -EINVAL;
  }
}

static void print_memory_regions(const memory_region_t *regions, size_t num) {
  printk(KERN_INFO "Number of regions: %zu\n", num);
  size_t i = 0;
  for (; i < num; i++) {
    printk(KERN_INFO "/dev/restore_memory: Region %zu: %lx-%lx %s %s %zu\n", i,
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

static int parse_dump_from_user(process_dump_t *dump, const char *buffer,
                                size_t len) {
  if (len != sizeof(process_dump_t)) {
    return -EINVAL;
  }

  // shallow copy of the dump struct: regs, num_regions
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

  restore_memory_config.state = ENTRY;

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