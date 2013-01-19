#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "vm/page.h"

/*
 * Helper structure for dealing with file system operations.
 */
struct file_descriptor {
	int fd_num;
	tid_t owner;
	struct file *file_struct;
	struct list_elem elem;
};

/* The list of all files openend by user programs. */
struct list open_files; 

static uint32_t *esp;

static void syscall_handler (struct intr_frame *);

/* System call functions */
static void halt (void);
static void exit (int);
static pid_t exec (const char *);
static int wait (pid_t);
static bool create (const char*, unsigned);
static bool remove (const char *);
static int open (const char *);
static int filesize (int);
static int read (int, void *, unsigned);
static int write (int, const void *, unsigned);
static void seek (int, unsigned);
static unsigned tell (int);
static void close (int);
static mapid_t mmap (int, void *);
static void munmap (mapid_t);
/* End of system call functions */


/* Helper functions*/
static struct file_descriptor *get_open_file_for_file_descriptor (int);
static void close_file_for_file_descriptor (int);
static bool is_valid_uvaddr (const void *);
static int allocated_file_descriptor (void);
bool is_stack_valid(struct intr_frame* f);

void syscall_init (void) {
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
	list_init (&open_files);
	lock_init (&file_system_lock);
}

static void syscall_handler (struct intr_frame *f) {
	esp = f->esp;

	if (is_stack_valid(f)) {
		exit (-1);
	}
	else {
		int syscall_number = *esp;
		switch (syscall_number) {
		case SYS_HALT:
			halt ();
			break;
		case SYS_EXIT:
			exit (*(esp + 1));
			break;
		case SYS_EXEC:
			f->eax = exec ((char *) *(esp + 1));
			break;
		case SYS_WAIT:
			f->eax = wait (*(esp + 1));
			break;
		case SYS_CREATE:
			f->eax = create ((char *) *(esp + 1), *(esp + 2));
			break;
		case SYS_REMOVE:
			f->eax = remove ((char *) *(esp + 1));
			break;
		case SYS_OPEN:
			f->eax = open ((char *) *(esp + 1));
			break;
		case SYS_FILESIZE:
			f->eax = filesize (*(esp + 1));
			break;
		case SYS_READ:
			f->eax = read (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
			break;
		case SYS_WRITE:
			f->eax = write (*(esp + 1), (void *) *(esp + 2), *(esp + 3));
			break;
		case SYS_SEEK:
			seek (*(esp + 1), *(esp + 2));
			break;
		case SYS_TELL:
			f->eax = tell (*(esp + 1));
			break;
		case SYS_CLOSE:
			close (*(esp + 1));
			break;
		case SYS_MMAP:
			f->eax = mmap (*(esp + 1), (void *) *(esp + 2));
			break;
		case SYS_MUNMAP:
			munmap (*(esp + 1));
			break;
		default:
			break;
		}
	}
}


/*
 * Terminate the current execution context, return status to kernel
 *
 */
void exit (int status) {
	struct child_status *child;
	struct thread *cur = thread_current ();
	printf ("%s: exit(%d)\n", cur->name, status);
	struct thread *parent = thread_get_by_id (cur->parent_id);

	if (parent != NULL) {
		struct list_elem *e = list_tail(&parent->children);

		/*
		 * Need to also return the status to the parent.
		 *
		 * Simply check the parent's child list and set the status to the proper position
		 */
		while ((e = list_prev (e)) != list_head (&parent->children)) {

			child = list_entry (e, struct child_status, elem_child_status);

			if (child->child_id == cur->tid) {
				lock_acquire (&parent->lock_child);
				child->is_exit_called = true;
				child->child_exit_status = status;
				lock_release (&parent->lock_child);
			}
		}
	}
	thread_exit ();
}

void halt (void) {
	shutdown_power_off ();
}

pid_t exec (const char *cmd_line) {
	tid_t tid;
	struct thread *cur;

	/* Check use pointer for validity */
	if (!is_valid_pointer (cmd_line)) {
		exit (-1);
	}

	cur = thread_current ();
	cur->child_load_status = 0;

	/*
	 * Call process execute.
	 */
	tid = process_execute (cmd_line);
	lock_acquire(&cur->lock_child);

	/*
	 * Make sure the child is loaded.
	 *
	 * Child is responsible for setting its status.
	 */
	while (cur->child_load_status == 0) {
		cond_wait(&cur->cond_child, &cur->lock_child);
	}

	/*
	 * Return -1 if the child isn't loaded.
	 */
	if (cur->child_load_status == -1){
		tid = -1;
	}

	lock_release(&cur->lock_child);
	return tid;
}

int wait (pid_t pid) {
	return process_wait(pid);
}

bool create (const char *file_name, unsigned size) {
	bool status;

	/*
	 * Check file name for validity.
	 */
	if (!is_valid_pointer (file_name)) {
		exit (-1);
	}

	lock_acquire (&file_system_lock);

	/*
	 * Magic
	 */
	status = filesys_create(file_name, size);
	lock_release (&file_system_lock);
	return status;
}

bool remove (const char *file_name) {
	bool status;
	if (!is_valid_pointer (file_name))
		exit (-1);

	lock_acquire (&file_system_lock);
	status = filesys_remove (file_name);
	lock_release (&file_system_lock);
	return status;
}

int open (const char *file_name) {
	struct file *f;
	struct file_descriptor *fd;
	int status = -1;

	/*
	 * Check argument for valid memory.
	 */
	if (!is_valid_pointer (file_name)) {
		exit (-1);
	}

	lock_acquire (&file_system_lock);

	/*
	 * Open the needed file.
	 */
	f = filesys_open (file_name);
	if (f != NULL) {
		/*
		 * Create file descriptor structure for it
		 */
		fd = calloc (1, sizeof *fd);
		fd->fd_num = allocated_file_descriptor ();
		fd->owner = thread_current ()->tid;
		fd->file_struct = f;
		list_push_back (&open_files, &fd->elem);
		status = fd->fd_num;
	}

	lock_release (&file_system_lock);
	return status;
}

int filesize (int fd) {
	struct file_descriptor *fd_struct;
	int status = -1;
	lock_acquire (&file_system_lock);

	/*
	 * Get the opened file and call file_length.
	 */
	fd_struct = get_open_file_for_file_descriptor (fd);
	if (fd_struct != NULL) {
		status = file_length (fd_struct->file_struct);
	}

	lock_release (&file_system_lock);
	return status;
}


int write (int fd, const void *buffer, unsigned size) {
	struct file_descriptor *fd_struct;
	int status = 0;

	unsigned buffer_size = size;
	void *temp = buffer;

	/* Check the output buffer to see if memory is valid */
	while (temp != NULL) {

		if (!is_valid_pointer (temp)) {
			exit (-1);
		}

		/* Go to the next position */
		if (buffer_size > PGSIZE) {
			temp += PGSIZE;
			buffer_size -= PGSIZE;
		}
		else if (buffer_size == 0) {
			/* Set stop condition */
			temp = NULL;
		} else {
			temp = buffer + size - 1;
			buffer_size = 0;
		}
	}

	lock_acquire (&file_system_lock);

	if (fd == STDIN_FILENO) {
		/*
		 * Can't write to stdin
		 */
		status = -1;
	} else if (fd == STDOUT_FILENO) {
		/*
		 * Print to screen
		 */
		putbuf (buffer, size);;
		status = size;
	} else {

		/*
		 * Write to misc buffer.
		 */
		fd_struct = get_open_file_for_file_descriptor (fd);

		if (fd_struct != NULL) {
			status = file_write (fd_struct->file_struct, buffer, size);
		}
	}
	lock_release (&file_system_lock);

	return status;
}


int read (int fd, void *buffer, unsigned size) {
	struct file_descriptor *fd_struct;
	int status = 0;
	struct thread *t = thread_current ();

	unsigned buffer_size = size;
	void * tmp = buffer;

	/* Check the output buffer to see if memory is valid */
	while (tmp != NULL) {
		if (!is_valid_uvaddr (tmp)) {
			exit (-1);
		}

		if (pagedir_get_page (t->pagedir, tmp) == NULL) {
			struct suppl_pte *spte;
			spte = get_suppl_pte (&t->suppl_page_table, pg_round_down (tmp));

			if (spte != NULL && !spte->is_loaded) {
				load_page (spte);
			} else if (spte == NULL && tmp >= (esp - 32)) {
				grow_stack (tmp);
			}
			else {
				exit (-1);
			}
		}

		/* Go to next position */
		if (buffer_size == 0) {
			/* terminate the checking loop */
			tmp = NULL;
		} else if (buffer_size > PGSIZE) {
			tmp += PGSIZE;
			buffer_size -= PGSIZE;
		} else {
			tmp = buffer + size - 1;
			buffer_size = 0;
		}
	}

	lock_acquire (&file_system_lock);
	if (fd == STDOUT_FILENO) {
		status = -1;
	} else if (fd == STDIN_FILENO) {
		uint8_t c;
		unsigned counter = size;
		uint8_t *buf = buffer;

		/*
		 * Read one character at a time from the console.
		 */
		while (counter > 1 && (c = input_getc()) != 0) {
			*buf = c;
			buffer++;
			counter--;
		}
		*buf = 0;
		status = size - counter;
	} else {
		/*
		 * Reading from a misc file, call file_read.
		 */
		fd_struct = get_open_file_for_file_descriptor (fd);
		if (fd_struct != NULL) {
			status = file_read (fd_struct->file_struct, buffer, size);
		}
	}
	lock_release (&file_system_lock);
	return status;
}


void seek (int fd, unsigned position) {
	struct file_descriptor *fd_struct;
	lock_acquire (&file_system_lock);

	/*
	 * Get the file and call file_seek.
	 */
	fd_struct = get_open_file_for_file_descriptor (fd);
	if (fd_struct != NULL) {
		file_seek (fd_struct->file_struct, position);
	}

	lock_release (&file_system_lock);
	return ;
}

unsigned tell (int fd) {
	struct file_descriptor *fd_struct;
	int status = 0;
	lock_acquire (&file_system_lock);

	/*
	 * Get the opened file
	 */
	fd_struct = get_open_file_for_file_descriptor (fd);

	/*
	 * Call file_tell to do the work.
	 */
	if (fd_struct != NULL) {
		status = file_tell (fd_struct->file_struct);
	}
	lock_release (&file_system_lock);
	return status;
}

void close (int fd) {
	struct file_descriptor *fd_struct;
	lock_acquire (&file_system_lock);

	/*
	 * Get the open file handler.
	 */
	fd_struct = get_open_file_for_file_descriptor (fd);

	/*
	 * Close the file.
	 */
	if (fd_struct != NULL && fd_struct->owner == thread_current ()->tid) {
		close_file_for_file_descriptor (fd);
	}
	lock_release (&file_system_lock);
	return ;
}

mapid_t
mmap (int fd, void *addr)
{
	struct file_descriptor *fd_struct;
	int32_t len;
	struct thread *t = thread_current ();
	int offset;

	/* Validating conditions to determine whether to reject the request */
	if (addr == NULL || addr == 0x0 || (pg_ofs (addr) != 0))
		return -1;

	/* Bad fds*/
	if(fd == 0 || fd == 1)
		return -1;
	fd_struct = get_open_file_for_file_descriptor (fd);
	if (fd_struct == NULL)
		return -1;

	/* file length not equal to 0 */
	len = file_length (fd_struct->file_struct);
	if (len <= 0)
		return -1;

	/* iteratively check if there is enough space for the file starting
	 * from the uvaddr addr*/
	offset = 0;
	while (offset < len)
	{
		if (get_suppl_pte (&t->suppl_page_table, addr + offset))
			return -1;

		if (pagedir_get_page (t->pagedir, addr + offset))
			return -1;

		offset += PGSIZE;
	}

	/* Add an entry in memory mapped files table, and add entries in
 supplemental page table iteratively which is in mmfiles_insert's
 semantic.
 If success, it will return the mapid;
 otherwise, return -1 */
	lock_acquire (&file_system_lock);
	struct file* newfile = file_reopen(fd_struct->file_struct);
	lock_release (&file_system_lock);
	return (newfile == NULL) ? -1 : mmfiles_insert (addr, newfile, len);
}

void
munmap (mapid_t mapping)
{
	/* Remove the entry in memory mapped files table, and remove corresponding
 entries in supplemental page table iteratively which is in
 mmfiles_remove()'s semantic. */
	mmfiles_remove (mapping);
}

/* Helper functions */

struct file_descriptor *get_open_file_for_file_descriptor (int fd) {
	struct list_elem *e;
	struct file_descriptor *fd_struct;
	e = list_tail (&open_files);

	/*
	 * Loop through the list of open files and return the file descriptor structure corresponding to the file descriptor.
	 */
	while ((e = list_prev (e)) != list_head (&open_files)) {
		fd_struct = list_entry (e, struct file_descriptor, elem);
		if (fd_struct->fd_num == fd) {
			return fd_struct;
		}
	}
	return NULL;
}

void close_file_for_file_descriptor (int fd) {

	struct list_elem *e;
	struct list_elem *prev;
	struct file_descriptor *fd_struct;
	e = list_end (&open_files);

	/*
	 * Lopp through the list of open files
	 */
	while (e != list_head (&open_files)) {
		prev = list_prev (e);
		fd_struct = list_entry (e, struct file_descriptor, elem);

		/*
		 * Close that file and exit.
		 */
		if (fd_struct->fd_num == fd) {
			list_remove (e);
			file_close (fd_struct->file_struct);
			free (fd_struct);

			/*
			 * Exiting is ok since file descriptor are unique.
			 */
			return ;
		}

		e = prev;
	}
	return ;
}

bool is_valid_pointer (const void *usr_ptr) {
	struct thread *cur = thread_current ();
	if (is_valid_uvaddr (usr_ptr))
	{
		return (pagedir_get_page (cur->pagedir, usr_ptr)) != NULL;
	}
	return false;
}

static bool is_valid_uvaddr (const void *uvaddr) {
	return (uvaddr != NULL && is_user_vaddr (uvaddr));
}

int allocated_file_descriptor () {
	static int fd_current = 1;
	return ++fd_current;
}

void close_files_for_thread (tid_t tid) {

	struct list_elem *e;
	struct list_elem *next;
	struct file_descriptor *fd_struct;
	e = list_begin (&open_files);

	while (e != list_tail (&open_files)) {
		next = list_next (e);
		fd_struct = list_entry (e, struct file_descriptor, elem);

		if (fd_struct->owner == tid) {
			list_remove (e);
			file_close (fd_struct->file_struct);
			free (fd_struct);
		}

		e = next;
	}
}

bool is_stack_valid(struct intr_frame* f) {
	return (!is_valid_pointer (esp) || !is_valid_pointer (esp + 1) ||
			!is_valid_pointer (esp + 2) || !is_valid_pointer (esp + 3));
}
