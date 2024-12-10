#ifndef KRESTORE_H
#define KRESTORE_H

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/types.h>

typedef struct {
  unsigned long start;
  unsigned long end;
  char permissions[5]; // e.g., "rwxp"
  char path[256];      // Path or descriptor (e.g., "[heap]")
  size_t size;
  char *content;
} memory_region_t;

typedef struct {
  struct user_regs_struct regs;
  size_t num_regions;
  memory_region_t *regions;
} process_dump_t;

// File operation functions
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);
static int parse_dump_from_user(process_dump_t *dump, const char *buffer,
                                size_t len);
static void print_memory_regions(const memory_region_t *regions, size_t num);

// Unmmap all regions in the current user program except the kernel-related
// ones.
static int unmap_all(void);

#endif