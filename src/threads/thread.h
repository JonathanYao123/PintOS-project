#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include "filesys/directory.h"
#include "threads/synch.h"
#include <debug.h>
#include <list.h>
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status
{
  THREAD_RUNNING, /* Running thread. */
  THREAD_READY,   /* Not running but ready to run. */
  THREAD_BLOCKED, /* Waiting for an event to trigger. */
  THREAD_DYING    /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t)-1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
{
  /* Owned by thread.c. */
  tid_t tid;                 /* Thread identifier. */
  enum thread_status status; /* Thread state. */
  char name[16];             /* Name (for debugging purposes). */
  uint8_t *stack;            /* Saved stack pointer. */
  int priority;              /* Priority. */
  struct list_elem allelem;  /* List element for all threads list. */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List element. */

#ifdef USERPROG
  /* Owned by userprog/process.c. */
  uint32_t *pagedir;         /* Page directory. */
  struct thread *parent;     /* Parent thread that spawned this. */
  struct list children;      /* Stores all children of this thread. */
  struct lock children_lock; /* Lock for accessing the children list. */
  struct list open_files;    /* All files this thread has open. */
  struct file *cur_file;     /* The current file this thread has open. */
  int cur_fd; /* The current max file descriptor that has been assigned by this
                 thread. */
  struct child *child_self; /* A pointer to the child structure that represents
                               this thread. */
#endif

  /* Project 4 (if we get to subdirectories). */
  struct dir *dir; /* The current working directory for this thread. */

  /* Owned by thread.c. */
  unsigned magic; /* Detects stack overflow. */
};

/* A structure to represent a child thread after it has died. */
struct child
{
  struct semaphore
      parent_sema;       /* Semaphore for parent waiting on this child. */
  int exit_status;       /* The exit status of the thread when it died. */
  struct lock lock;      /* A lock to ensure synchronized access. */
  struct thread *self;   /* A pointer to the thread this represents. */
  struct list_elem elem; /* List element for children list. */
  struct semaphore
      load_sema; /* A semaphore for telling parent when exec has loaded. */
  bool loaded;   /* Whether the exec loaded successfully or not. */
  struct semaphore exit_sema; /* A semaphore for waiting on the parent to
                                 obtain the exit status. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

struct thread *find_thread (tid_t id);
struct child *find_child (tid_t id, struct thread *parent);
void remove_all_children (struct thread *cur);
struct open_file *find_open_file (int fd, struct thread *cur);
void close_all_open_files (struct thread *cur);

#endif /* threads/thread.h */
