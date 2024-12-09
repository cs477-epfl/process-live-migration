#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/types.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#define DEVICE_NAME "restore_memory"
#define CLASS_NAME "virtual"

static int major_number;
static struct class *device_class = NULL;
static struct device *device_device = NULL;

typedef struct memory_region_t {
  unsigned long start;
  unsigned long end;
  char permissions[5]; // e.g., "rwxp"
  char path[256];      // Path or descriptor (e.g., "[heap]")
  size_t size;
  char *content;
};

// File operation functions
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);
static pid_t read_pid_from_user(const char *buffer, size_t len);
static struct mm_struct *get_memory_struct(pid_t pid);
static int read_map_region_from_user(struct memory_region_t *region,
                                     const char *buffer, size_t len);

static struct file_operations fops = {
    .open = device_open,
    .read = device_read,
    .write = device_write,
    .release = device_release,
};

enum dev_state {
  ENTRY,   // initial state, device is free
  WAITPID, // waiting for the first write to get pid
  MAPPING, // waiting for following writes to get memory mapping and contents
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
  restore_memory_config.state = WAITPID;
  return 0;
}

static int device_release(struct inode *inodep, struct file *filep) {
  restore_memory_config.state = ENTRY;
  restore_memory_config.pid = 0;
  printk(KERN_INFO "/dev/restore_memory: Device has been closed\n");
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
  case WAITPID:
    pid_t pid = read_pid_from_user(buffer, len);
    if (pid < 0) {
      return pid;
    }
    restore_memory_config.pid = pid;
    struct mm_struct *mm = get_memory_struct(pid);
    if (mm == NULL) {
      return -EINVAL;
    }
    restore_memory_config.state = MAPPING;
    printk("/dev/restore_memory: pid: %d\n", pid);
    return len;

  case MAPPING:
    struct memory_region_t region;
    int ret = read_map_region_from_user(&region, buffer, len);
    if (ret < 0) {
      return ret;
    }
    // TODO: restore memory region

    if (region.content != NULL) {
      kfree(region.content);
    }
    return sizeof(struct memory_region_t);

  default:
    printk(KERN_ALERT "/dev/restore_memory: invalid state at write\n");
    return -EINVAL;
  }
}

static pid_t read_pid_from_user(const char *buffer, size_t len) {
  pid_t pid;
  if (len != sizeof(pid_t)) {
    return -EINVAL;
  }
  if (copy_from_user(&pid, buffer, len) != 0) {
    return -EFAULT;
  }
  return pid;
}

static int read_map_region_from_user(struct memory_region_t *region,
                                     const char *buffer, size_t len) {
  if (len != sizeof(struct memory_region_t)) {
    return -EINVAL;
  }
  if (copy_from_user(region, buffer, len) != 0) {
    return -EFAULT;
  }
  if (region->content != NULL) {
    // pre-copy, should read memory contents
    char *user_content = region->content;
    region->content = kmalloc(region->size, GFP_KERNEL);
    if (region->content == NULL) {
      return -ENOMEM;
    }
    if (copy_from_user(region->content, user_content, region->size) != 0) {
      kfree(region->content);
      return -EFAULT;
    }
  }
  return 0;
}

static struct mm_struct *get_memory_struct(pid_t pid) {
  rcu_read_lock();
  struct task_struct *task = pid_task(find_vpid(pid), PIDTYPE_PID);
  if (task == NULL) {
    rcu_read_unlock();
    return NULL;
  }
  struct mm_struct *mm = get_task_mm(task);
  return mm;
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