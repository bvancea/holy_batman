#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/synch.h"

/* The lock used by system calls involving file system to ensure only one thread
at a time is accessing file system */
struct lock filesys_lock;

void syscall_init (void);

struct file_descriptor {
  int fd_num;
  tid_t owner;
  struct file *file_struct;
  struct list_elem elem;
};

/* a list of open files, represents all the files open by the user process
through syscalls. */
struct list open_files;

#endif /* userprog/syscall.h */
