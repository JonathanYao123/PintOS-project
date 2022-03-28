#include "userprog/syscall.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include <syscall-nr.h>

#define STDIN 0
#define STDOUT 1

static void syscall_handler (struct intr_frame *);
static struct lock filesys_lock;

void
syscall_init (void)
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{

  struct thread *cur = thread_current ();
  validate_page_ptr (cur->pagedir, (const void *)f->esp);

  int args[3];
  void *page_ptr;
  int sys_code = *(int *)f->esp;

  switch (sys_code)
    {
    case SYS_HALT:
      halt ();
      break;
    case SYS_EXIT:
      /* status */
      get_stack_args (f, &args[0], 1);
      exit (args[0]);
      break;
    case SYS_EXEC:
      /* file name */
      get_stack_args (f, &args[0], 1);
      args[0] = validate_page_ptr (cur->pagedir, (const void *)args[0]);
      f->eax = exec ((const char *)args[0]);
      break;
    case SYS_WAIT:
      /* pid */
      get_stack_args (f, &args[0], 1);
      f->eax = (int)wait ((pid_t)args[0]);
      break;
    case SYS_CREATE:
      /* file name, size */
      get_stack_args (f, &args[0], 2);
      validate_buffer ((void *)args[0], (unsigned *)args[1]);
      args[0] = validate_page_ptr (cur->pagedir, (const void *)args[0]);
      f->eax = create ((const char *)args[0], (unsigned)args[1]);
      break;
    case SYS_REMOVE:
      /* file name */
      get_stack_args (f, &args[0], 1);
      args[0] = validate_page_ptr (cur->pagedir, (const void *)args[0]);
      f->eax = remove ((const char *)args[0]);
      break;
    case SYS_WRITE:
      /* fd, buffer, size */
      get_stack_args (f, &args[0], 3);
      validate_buffer ((void *)args[1], args[2]);
      args[1] = validate_page_ptr (cur->pagedir, (const void *)args[1]);
      f->eax = write (args[0], (void *)args[1], (unsigned)args[2]);
      break;
    case SYS_READ:
      /* fd, buffer, size */
      get_stack_args (f, &args[0], 3);
      validate_buffer ((void *)args[1], args[2]);
      args[1] = validate_page_ptr (cur->pagedir, (const void *)args[1]);
      f->eax = read (args[0], (const void *)args[1], (unsigned)args[2]);
      break;
    case SYS_OPEN:
      /* file name */
      get_stack_args (f, &args[0], 1);
      args[0] = validate_page_ptr (cur->pagedir, (const void *)args[0]);
      f->eax = open ((const char *)args[0]);
      break;
    case SYS_FILESIZE:
      /* fd */
      get_stack_args (f, &args[0], 1);
      f->eax = filesize (args[0]);
      break;
    case SYS_SEEK:
      /* fd, position */
      get_stack_args (f, &args[0], 2);
      seek (args[0], (unsigned)args[1]);
      break;
    case SYS_TELL:
      /* fd */
      get_stack_args (f, &args[0], 1);
      f->eax = tell (args[0]);
      break;
    case SYS_CLOSE:
      /* fd */
      get_stack_args (f, &args[0], 1);
      close ((const void *)args[0]);
      break;
    case SYS_CHDIR:
      /* file name */
      get_stack_args (f, &args[0], 1);
      f->eax = chdir (args[0]);
      break;
    case SYS_MKDIR:
      /* file name */
      get_stack_args (f, &args[0], 1);
      f->eax = mkdir (args[0]);
      break;
    case SYS_READDIR:
      /* fd, file name */
      get_stack_args (f, &args[0], 2);
      f->eax = readdir (args[0], args[1]);
      break;
    case SYS_ISDIR:
      /* fd */
      get_stack_args (f, &args[0], 1);
      f->eax = isdir (args[0]);
      break;
    case SYS_INUMBER:
      /* fd */
      get_stack_args (f, &args[0], 1);
      f->eax = inumber (args[0]);
      break;
    default:
      printf ("ERROR: system call not implemented!");
      exit (EXIT_FAILURE);
      break;
    }
}

void
halt (void)
{
  shutdown_power_off ();
}

void
exit (int status)
{
  struct thread *cur = thread_current ();

  cur->child_self->exit_status = status;

  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

int
wait (pid_t pid)
{
  return process_wait (pid);
}

pid_t
exec (const char *cmd_line)
{
  if (!cmd_line)
    {
      return EXIT_FAILURE;
    }
  lock_acquire (&filesys_lock);
  pid_t child_tid = process_execute (cmd_line);
  lock_release (&filesys_lock);
  return child_tid;
}

int
write (int fd, const void *buffer, unsigned size)
{
  struct thread *cur = thread_current ();
  if (fd == STDOUT)
    {
      putbuf (buffer, size);
      return size;
    }

  if (fd == STDIN || list_empty (&cur->open_files))
    {
      return 0;
    }

  if (size <= 0)
    {
      return size;
    }

  lock_acquire (&filesys_lock);
  struct open_file *of = find_open_file (fd, cur);
  if (of == NULL)
    {
      lock_release (&filesys_lock);
      return EXIT_FAILURE;
    }

  int bytes = (int)file_write (of->file, buffer, size);

  lock_release (&filesys_lock);
  return bytes;
};

bool
create (const char *file, unsigned initial_size)
{
  lock_acquire (&filesys_lock);
  bool status = filesys_create (file, initial_size, false);
  lock_release (&filesys_lock);
  return status;
};

bool
remove (const char *file)
{
  if (file == NULL)
    return EXIT_FAILURE;
  lock_acquire (&filesys_lock);
  bool status = filesys_remove (file);
  lock_release (&filesys_lock);
  return status;
}

int
open (const char *file)
{
  if (file == NULL)
    return EXIT_FAILURE;
  struct thread *cur = thread_current ();

  /* allocate open_file struct */
  struct open_file *of = palloc_get_page (PAL_ZERO);
  if (of == NULL)
    return EXIT_FAILURE;

  lock_acquire (&filesys_lock);
  of->file = filesys_open (file);
  if (of->file == NULL)
    {
      palloc_free_page (of);
      lock_release (&filesys_lock);
      return EXIT_FAILURE;
    }

  of->fd = cur->cur_fd;
  cur->cur_fd++;
  list_push_front (&cur->open_files, &of->elem);
  lock_release (&filesys_lock);
  return of->fd;
}

int
filesize (int fd)
{
  struct thread *cur = thread_current ();

  lock_acquire (&filesys_lock);
  struct open_file *of = find_open_file (fd, cur);

  if (of == NULL)
    {
      lock_release (&filesys_lock);
      return EXIT_FAILURE;
    }

  int length = file_length (of->file);
  lock_release (&filesys_lock);
  return length;
}

int
read (int fd, void *buffer, unsigned size)
{
  struct thread *cur = thread_current ();
  lock_acquire (&filesys_lock);

  if (fd == STDIN)
    {
      lock_release (&filesys_lock);
      return (int)input_getc ();
    }

  /* can't read from STDOUT or non-existent file */
  if (fd == STDOUT || list_empty (&cur->open_files))
    {
      lock_release (&filesys_lock);
      return 0;
    }

  struct open_file *of = find_open_file (fd, cur);
  if (of == NULL)
    {
      lock_release (&filesys_lock);
      return EXIT_FAILURE;
    }

  lock_release (&filesys_lock);
  int bytes = (int)file_read (of->file, buffer, size);
  return bytes;
}

void
seek (int fd, unsigned position)
{
  struct thread *cur = thread_current ();
  lock_acquire (&filesys_lock);
  if (list_empty (&cur->open_files))
    {
      lock_release (&filesys_lock);
      return;
    }
  struct open_file *of = find_open_file (fd, cur);
  if (of == NULL)
    {
      lock_release (&filesys_lock);
      return;
    }
  file_seek (of->file, position);
  lock_release (&filesys_lock);

  return;
}

unsigned
tell (int fd)
{
  struct thread *cur = thread_current ();
  lock_acquire (&filesys_lock);
  if (list_empty (&cur->open_files))
    {
      lock_release (&filesys_lock);
      return EXIT_FAILURE;
    }
  struct open_file *of = find_open_file (fd, cur);
  if (of == NULL)
    {
      lock_release (&filesys_lock);
      return EXIT_FAILURE;
    }
  unsigned pos = file_tell (of->file);
  lock_release (&filesys_lock);

  return pos;
}

void
close (int fd)
{
  struct thread *cur = thread_current ();
  lock_acquire (&filesys_lock);
  if (list_empty (&cur->open_files))
    {
      lock_release (&filesys_lock);
      return;
    }
  struct open_file *of = find_open_file (fd, cur);
  if (of == NULL)
    {
      lock_release (&filesys_lock);
      return;
    }
  file_close (of->file);
  list_remove (&of->elem);
  lock_release (&filesys_lock);

  return;
}

bool
chdir (const char *dir)
{
  return false;
}

bool
mkdir (const char *dir)
{
  lock_acquire (&filesys_lock);
  bool ret = filesys_create (dir, 0, true);
  lock_release (&filesys_lock);

  return ret;
}

bool
readdir (int fd, char *name)
{
  return false;
}

bool
isdir (int fd)
{
  struct thread *cur = thread_current ();
  lock_acquire (&filesys_lock);

  struct open_file *of = find_open_file (fd, cur);
  if (of == NULL)
    {
      lock_release (&filesys_lock);
      return EXIT_FAILURE;
    }
  bool ret = (int)inode_is_directory (file_get_inode (of->file));

  lock_release (&filesys_lock);
  return ret;
}

int
inumber (int fd)
{
  struct thread *cur = thread_current ();
  lock_acquire (&filesys_lock);

  struct open_file *of = find_open_file (fd, cur);
  if (of == NULL)
    {
      lock_release (&filesys_lock);
      return EXIT_FAILURE;
    }
  int ret = (int)inode_get_inumber (file_get_inode (of->file));

  lock_release (&filesys_lock);
  return ret;
}

int
validate_page_ptr (uint32_t pagedir, const void *page_ptr)
{
  page_ptr = pagedir_get_page (pagedir, (const void *)page_ptr);

  if (page_ptr == NULL)
    exit (EXIT_FAILURE);

  return (int)page_ptr;
}

/* Validates that the provided address exists and is within the user address
 * space. */
void
validate_addr (const void *addr)
{
  if (addr == NULL || !is_user_vaddr (addr) || addr < 0x08048000)
    {
      exit (EXIT_FAILURE);
    }
};

/* Validates all addresses within the provided buffer. */
void
validate_buffer (void *buffer, unsigned size)
{
  int i;
  char *ptr = (char *)buffer;
  for (i = 0; i < size; i++)
    {
      validate_addr ((const void *)ptr);
      ptr++;
    }
}

/* Pulls the arguments for this command from the stack. */
void
get_stack_args (struct intr_frame *f, int *argv, int argc)
{
  int i, *ptr;
  for (i = 0; i < argc; i++)
    {
      ptr = (int *)f->esp + i + 1;
      validate_addr ((const void *)ptr);
      argv[i] = *ptr;
    }
}