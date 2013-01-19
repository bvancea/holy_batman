#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/thread.h"

struct frame_struct {
  void *frame;				// Frame address
  tid_t tid;				// Thread holding it
  uint32_t *pte;			// The supplementary page table entry associated to it

  void *uva;
  struct list_elem elem;
};

struct list vm_frames;

/* Frame table operations */
void init_frame (void);
void *allocate_frame (enum palloc_flags flags);
void free_frame (void *);
void frame_set_pte (void*, uint32_t *, void *);
void *evict_frame (void);

#endif /* vm/frame.h */
