#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"
#include "userprog/process.h"

/* the lock used by syscalls involving file system to ensure only one thread
   at a time is accessing file system */
struct lock file_system_lock;

void syscall_init (void);
bool is_valid_pointer (const void *);
void close_files_for_thread (tid_t);
#endif /* userprog/syscall.h */
