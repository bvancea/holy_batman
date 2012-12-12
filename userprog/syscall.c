#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);

/*System call functions */
static void syscall_halt (void);
static void syscall_exit (int);
static int syscall_wait (pid_t);

static bool syscall_create (const char*, unsigned);
static bool syscall_remove (const char *);
static int syscall_open (const char *);
static int syscall_filesize (int);
static int syscall_read (int, void *, unsigned);
static int syscall_write (int, const void *, unsigned);
static void syscall_seek (int, unsigned);
static unsigned syscall_tell (int);
static void syscall_close (int);

static int syscall_exec (const char *);

/* Helper functions*/
static bool is_valid_uvaddr (const void *);
bool is_valid_pointer (const void *usr_ptr);
struct file_descriptor * get_open_file(int fd);


void syscall_init (void) {
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  list_init (&open_files);
  lock_init (&filesys_lock);
}

static void syscall_handler(struct intr_frame *f) {

	if (!is_valid_stack(f)) {
		//TODO replace with exit system call
		thread_exit();
	} else {
		int syscall_no = ((int*) f->esp)[0];

			/*int argc = ((int*) f->esp)[1];
			char* argv = ((int*) f->esp)[2];
			char* command = ((int *) f->esp)[3];
			printf("%d %d %s %s \n", syscall_no, argc, argv, command);
			*/
		switch (syscall_no) {
		case SYS_EXIT:
			//printf("SYS_EXIT system call!\n");
			thread_exit();
			return;
		case SYS_HALT:
			//printf("SYS_HALT system call!\n");
			syscall_halt();
			return;
		case SYS_EXEC:
			//printf("SYS_EXEC system call!\n");
			f->eax = syscall_exec (((int*) f->esp)[1]);
			return;
		case SYS_WAIT:
			//printf("SYS_WAIT system call!\n");
			f->eax = syscall_wait (((int*) f->esp)[1]);
			return;
		case SYS_CREATE:
			//printf("SYS_CREATE system call!\n");
			f->eax = syscall_create (((int*) f->esp)[1], ((int*) f->esp)[1]);
			return;
		case SYS_OPEN:
			printf("SYS_OPEN system call!\n");
			f->eax = syscall_open (((int*) f->esp)[1]);
			return;
		case SYS_FILESIZE:
			//printf("SYS_FILESIZE system call!\n");
			f->eax = syscall_filesize(((int*) f->esp)[1]);
			return;
		case SYS_READ:
			//printf("SYS_WRITE system call!\n");
			///int fd = ((int *) f->esp)[1];
			//void *buffer = (f->esp + 2);
			//unsigned length =((unsigned *) f->esp)[3];
			f->eax = syscall_read (((int *) f->esp)[1],((int *) f->esp)[2],((int *) f->esp)[3]);
			return;
		case SYS_WRITE:
			//printf("SYS_WRITE system call!\n");
			//int fd = ((int *) f->esp)[1];
			//void *buffer = (f->esp + 2);
			//unsigned length =((unsigned *) f->esp)[3];
			f->eax = syscall_write (((int *) f->esp)[1],((int *) f->esp)[2],((int *) f->esp)[3]);
			return;
		case SYS_SEEK:
			//printf("SYS_SEEK system call!\n");
			syscall_seek(((int*) f->esp)[1],((int *) f->esp)[2]);
			break;
		case SYS_TELL:
			f->eax = syscall_tell(((int*) f->esp)[1]);
			return;
		case SYS_CLOSE:
			//printf("SYS_CLOSE system call!\n");
			syscall_close(((int*) f->esp)[1]);
			return;
		case SYS_REMOVE:
			//printf("SYS_CLOSE system call!\n");
			f->eax = syscall_remove(((int*) f->esp)[1]);
			return;
		}
		thread_exit();
	}
}


/* Syscall functions */
static void syscall_halt (void) {
	shutdown_power_off ();
}

static int syscall_write (int fd, const void *buffer, unsigned size) {
	int status = 0;
	char *cbuff = (char *) buffer;

	unsigned buffer_pos = size;
	void *temp_buffer = buffer;

	/* check the user memory pointing by buffer are valid*/
	while (temp_buffer != NULL) 	{
		if (!is_valid_pointer (temp_buffer)) {
			syscall_exit (-1);
		}
		/* Advance */
		if (buffer_pos > PGSIZE)
		{
			temp_buffer += PGSIZE;
			buffer_pos -= PGSIZE;
		}
		else if (buffer_pos == 0) {
			/* terminate the checking loop */
			temp_buffer = NULL;
		}
		else {
			/* last loop */
			temp_buffer = buffer + size - 1;
			buffer_pos = 0;
		}
	}

	lock_acquire (&filesys_lock);
	if (fd == STDIN_FILENO) {
		status = -1;
	} else if (fd == STDOUT_FILENO) {
		putbuf (buffer, size);
		status = size;
	} else {
		struct file_descriptor *fd_struct = get_open_file (fd);
		if (fd_struct != NULL) {
			status = file_write (fd_struct->file_struct, buffer, size);
		}
	}
	lock_release (&filesys_lock);

	return status;
}

static void syscall_exit (int status) {
	thread_exit ();
}

static int syscall_wait (int pid ) {
	return process_wait(pid);
}

static int syscall_exec (const char *cmd_line) {
	 /* a thread's id. When there is a user process within a kernel thread, we
	 * use one-to-one mapping from tid to pid, which means pid = tid
	 */
	tid_t tid;
	//struct thread *cur;

	/* check if the user pinter is valid */
	if (!is_valid_pointer(cmd_line)) {
		syscall_exit(-1);
	}

	//cur = thread_current();
	tid = process_execute(cmd_line);
	return tid;
}

static bool syscall_create (const char* file_name, unsigned size) {
	bool status;

	if (!is_valid_pointer(file_name))
		syscall_exit(-1);

	lock_acquire(&filesys_lock);
	status = filesys_create(file_name, size);
	lock_release(&filesys_lock);
	return status;
}

static bool syscall_remove (const char *file_name) {
	bool status;
	if (!is_valid_pointer(file_name))
		syscall_exit(-1);

	lock_acquire(&filesys_lock);
	status = filesys_remove(file_name);
	lock_release(&filesys_lock);
	return status;
}

static int syscall_open(const char *file_name) {
	struct file *f;
	struct file_descriptor *fd;
	int status = -1;

	if (!is_valid_pointer(file_name))
		syscall_exit(-1);

	//printf("Before lock\n");
	lock_acquire(&filesys_lock);

	f = filesys_open(file_name);
	if (f != NULL) {
		//printf("Inside\n");
		fd = calloc(1, sizeof *fd);
		fd->fd_num = allocate_fd();
		fd->owner = thread_current()->tid;
		fd->file_struct = f;
		list_push_back(&open_files, &fd->elem);
		status = fd->fd_num;
	}
	lock_release(&filesys_lock);

	//printf("After lock\n");
	return status;
}

static int syscall_filesize(int fd) {
	struct file_descriptor *fd_struct;
	int status = -1;
	lock_acquire(&filesys_lock);
	fd_struct = get_open_file(fd);
	if (fd_struct != NULL)
		status = file_length(fd_struct->file_struct);
	lock_release(&filesys_lock);
	return status;
}

static int syscall_read (int fd, void *buffer, unsigned size) {
	 struct file_descriptor *fd_struct;
	int status = 0;
	struct thread *t = thread_current();

	unsigned buffer_size = size;
	void *buffer_tmp = buffer;

	/* check the user memory pointing by buffer are valid */
	while (buffer_tmp != NULL) {
		if (!is_valid_uvaddr(buffer_tmp))
			syscall_exit(-1);

		/* Advance */
		if (buffer_size == 0) {
			/* terminate the checking loop */
			buffer_tmp = NULL;
		} else if (buffer_size > PGSIZE) {
			buffer_tmp += PGSIZE;
			buffer_size -= PGSIZE;
		} else {
			/* last loop */
			buffer_tmp = buffer + size - 1;
			buffer_size = 0;
		}
	}

	lock_acquire(&filesys_lock);
	if (fd == STDOUT_FILENO)
		status = -1;
	else if (fd == STDIN_FILENO) {
		uint8_t c;
		unsigned counter = size;
		uint8_t *buf = buffer;
		while (counter > 1 && (c = input_getc()) != 0) {
			*buf = c;
			buffer++;
			counter--;
		}
		*buf = 0;
		status = size - counter;
	} else {
		fd_struct = get_open_file(fd);
		if (fd_struct != NULL)
			status = file_read(fd_struct->file_struct, buffer, size);
	}

	lock_release(&filesys_lock);
	return status;
}

static void syscall_seek(int fd, unsigned position) {
	struct file_descriptor *fd_struct;
	lock_acquire(&filesys_lock);
	fd_struct = get_open_file(fd);
	if (fd_struct != NULL)
		file_seek(fd_struct->file_struct, position);
	lock_release(&filesys_lock);
	return;
}

static unsigned syscall_tell(int fd) {
	struct file_descriptor *fd_struct;
	int status = 0;
	lock_acquire(&filesys_lock);
	fd_struct = get_open_file(fd);
	if (fd_struct != NULL)
		status = file_tell(fd_struct->file_struct);
	lock_release(&filesys_lock);
	return status;
}

static void syscall_close (int fd ) {
	struct file_descriptor *fd_struct;
	lock_acquire (&filesys_lock);
	fd_struct = get_open_file (fd);
	if (fd_struct != NULL && fd_struct->owner == thread_current ()->tid) {
	  close_open_file (fd);
	}
	lock_release (&filesys_lock);
	return ;
}

/*
 * Helper functions
 */

/*
 * Close the file having the file descriptor fd.
 *
 */
void close_open_file(int fd) {
	struct list_elem *e;
	struct list_elem *prev;
	struct file_descriptor *fd_struct;
	e = list_end(&open_files);

	/*
	 * Check the list of opened files
	 */
	while (e != list_head(&open_files)) {
		prev = list_prev(e);
		fd_struct = list_entry (e, struct file_descriptor, elem);

		/*
		 * Find the file pointer
		 */
		if (fd_struct->fd_num == fd) {
			list_remove(e);
			file_close(fd_struct->file_struct); /* Close file */
			free(fd_struct);
			return;
		}
		e = prev;
	}
	return;
}

struct file_descriptor * get_open_file(int fd) {
	struct list_elem *e;
	struct file_descriptor *fd_struct;
	e = list_tail(&open_files);
	while ((e = list_prev(e)) != list_head(&open_files)) {
		fd_struct = list_entry (e, struct file_descriptor, elem);
		if (fd_struct->fd_num == fd) {
			return fd_struct;
		}
	}
	return NULL;
}


bool is_valid_stack(struct intr_frame *f) {
	if (!is_valid_pointer(f->esp) ||
			!is_valid_pointer(f->esp + 1) ||
			!is_valid_pointer(f->esp + 2) ||
			!is_valid_pointer(f->esp + 3)) {
		return false;
	} else {
		return true;
	}

}
/*
 * Check if a user memory pointer is valid.
 *
 * This means: not null, inside the user virtual address space and mapped on a page.
 */
bool is_valid_pointer (const void *usr_ptr) {
  struct thread *cur = thread_current ();
  if (is_valid_uvaddr (usr_ptr)) {
	  // check if it the address is mapped or not
      return (pagedir_get_page (cur->pagedir, usr_ptr)) != NULL;
  }
  return false;
}
/*
 *
 */
static bool is_valid_uvaddr (const void *uvaddr) {
  return (uvaddr != NULL && is_user_vaddr (uvaddr));
}


int
allocate_fd ()
{
  static int fd_current = 1;
  return ++fd_current;
}
