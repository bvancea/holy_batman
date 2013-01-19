#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "lib/kernel/list.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "threads/pte.h"
#include "vm/swap.h"

#include "vm/frame.h"

/* Lock for synchronizing access to fram table */
static struct lock vm_lock;

/* Lock to synchronized eviction*/
static struct lock eviction_lock;

/*
 * Operations that shouldn't be accessed from the header.
 */
static bool add_frame (void *);
static void remove_frame (void *);
static struct frame_struct *get_frame_structure (void *);
static struct frame_struct *get_frame_to_evict (void);
static bool save_evicted_frame (struct frame_struct *);


/*
 * Initialize frame table and related locks
 */
void init_frame () {
	list_init (&vm_frames);
	lock_init (&vm_lock);
	lock_init (&eviction_lock);
}

/*
 * Allocated a frame from the user pool page and add it to the table
 * */
void *allocate_frame (enum palloc_flags flags) {
	void *frame = NULL;

	/* Try to allocate a page from user pool */
	if (flags & PAL_USER) {
		if (flags & PAL_ZERO) {
			frame = palloc_get_page (PAL_USER | PAL_ZERO);
		} else {
			frame = palloc_get_page (PAL_USER);
		}
	}

	/*
	 * If the frame was allocated succesfully, add it to the table.
	 *
	 * If not, evict a frame to make room for it.
	 */
	if (frame != NULL) {
		add_frame (frame);
	} else if ((frame = evict_frame ()) == NULL) {
		PANIC ("Evicting frame failed");
	}
	return frame;
}

/*
 * Remove the frame entry and free the page.
 */
void free_frame (void *frame) {
	remove_frame (frame);
	palloc_free_page (frame);
}

/*
 * Set the page table entry for the frame
 */
void frame_set_pte (void *frame, uint32_t *pte, void *upage) {
	struct frame_struct *vf;
	vf = get_frame_structure (frame);
	if (vf != NULL) {
		vf->pte = pte;
		vf->uva = upage;
	}
}

/*
 * 	Evict a frame from the table and save it on the swap partition.
 */
void * evict_frame () {
	bool result;
	struct frame_struct *vf;
	struct thread *t = thread_current ();

	lock_acquire (&eviction_lock);

	/*
	 * Find the "lucky" frame
	 */
	vf = get_frame_to_evict ();
	if (vf == NULL)
		PANIC ("No frame to evict.");

	/*
	 * Save to swap.
	 */
	result = save_evicted_frame (vf);
	if (!result) {
		PANIC ("can't save evicted frame");
	}

	/*
	 * Clear the pte entry and page to the current frame.
	 */
	vf->tid = t->tid;
	vf->pte = NULL;
	vf->uva = NULL;

	lock_release (&eviction_lock);

	return vf->frame;
}

/*
 * Selecting the frame to evict is done using a "second chance" algorithm.
 *
 * Scan the list of frames and aim to find one that wasn't accessed. If all were accessed, just return the first entry.
 * */
static struct frame_struct *get_frame_to_evict () {
	struct frame_struct *vf;
	struct thread *t;
	struct list_elem *e;

	struct frame_struct *evicted = NULL;

	int round = 1;
	bool found = false;

	while (!found) {
		e = list_head (&vm_frames);

		/*
		 * Try to find a frame that wasn't accessed.
		 */
		while ((e = list_next (e)) != list_tail (&vm_frames)) {
			vf = list_entry (e, struct frame_struct, elem);
			t = thread_get_by_id (vf->tid);
			bool accessed  = pagedir_is_accessed (t->pagedir, vf->uva);
			if (!accessed) {
				evicted = vf;
				list_remove (e);
				list_push_back (&vm_frames, e);
				break;
			} else {
				pagedir_set_accessed (t->pagedir, vf->uva, false);
			}
		}

		/*
		 * Make sure the loop executes at most 2 times.
		 */
		if (evicted != NULL) {
			found = true;				// exit on first time
		} else if (round++ == 2){
			found = true;				// exit on second
		}
	}

	return evicted;
}

/*
 * Save evicted frame's content for swapping.
 */
static bool save_evicted_frame (struct frame_struct *vf) {
	struct thread *t;
	struct suppl_pte *spte;

	/* Get corresponding thread */
	t = thread_get_by_id (vf->tid);

	/* Get suppl page table entry */
	spte = get_suppl_pte (&t->suppl_page_table, vf->uva);

	/* If there's no entry, create one and insert it. */
	if (spte == NULL) {
		spte = calloc(1, sizeof *spte);
		spte->uvaddr = vf->uva;
		spte->type = SWAP;
		if (!insert_suppl_pte (&t->suppl_page_table, spte)) {
			return false;
		}
	}

	size_t swap_slot_idx;
	/*
	 * Check if it is a mmf page that has modifications, if so - write back to file.
	 *
	 * If its some other type of page, swap it out.
	 *
	 * Clean pages are swapped out without other actions.
	 */
	if (pagedir_is_dirty (t->pagedir, spte->uvaddr)  && (spte->type == MMF)) {
		write_back_dirty_mmf_page (spte);
	} else if (pagedir_is_dirty (t->pagedir, spte->uvaddr) || (spte->type != FILE)) {
		swap_slot_idx = vm_swap_out (spte->uvaddr);
		if (swap_slot_idx == SWAP_ERROR) {
			return false;
		}

		spte->type = spte->type | SWAP;
	}

	memset (vf->frame, 0, PGSIZE);

	/*
	 * Update swap attributes
	 */
	spte->swap_slot_idx = swap_slot_idx;
	spte->swap_writable = *(vf->pte) & PTE_W;
	spte->is_loaded = false;

	/*
	 * The frame no longer needs to be in the process' pagedir.
	 */
	pagedir_clear_page (t->pagedir, spte->uvaddr);

	return true;
}

/*
 * Add an entry to frame table.
 *
 * Didn't place it in header because it should be visible from the outside.
 */
static bool add_frame (void *frame) {
	struct frame_struct *vf;
	vf = calloc (1, sizeof *vf);

	/*
	 * Sanity check for frame.
	 */
	if (vf == NULL) {
		return false;
	}

	vf->tid = thread_current ()->tid;
	vf->frame = frame;

	lock_acquire (&vm_lock);

	/*
	 * Simply add it to the list.
	 */
	list_push_back (&vm_frames, &vf->elem);
	lock_release (&vm_lock);

	return true;
}

/*
 * Remove the entry from frame table and free the memory space
 *
 * Didn't place it in header because it should be visible from the outside.
 */
static void remove_frame (void *frame) {
	struct frame_struct *vf;
	struct list_elem *e;

	lock_acquire (&vm_lock);
	e = list_head (&vm_frames);
	while ((e = list_next (e)) != list_tail (&vm_frames)) {
		vf = list_entry (e, struct frame_struct, elem);
		/*
		 * Once found, remove the frame from the list and clean the memory handle.
		 */
		if (vf->frame == frame) {
			list_remove (e);
			free (vf);
			break;
		}
	}

	lock_release (&vm_lock);
}

/*
 * Get the frame structure corresponding to the page frame address.
 *
 * Didn't place it in header because it should be visible from the outside.
 */
static struct frame_struct *get_frame_structure (void *frame) {
	struct frame_struct *vf;
	struct list_elem *e;

	lock_acquire (&vm_lock);
	e = list_head (&vm_frames);

	while ((e = list_next (e)) != list_tail (&vm_frames)) {
		vf = list_entry (e, struct frame_struct, elem);
		if (vf->frame == frame) {
			/*
			 * Found the desired frame, stop and return it.
			 */
			break;
		}
		vf = NULL;
	}
	lock_release (&vm_lock);

	return vf;
}
