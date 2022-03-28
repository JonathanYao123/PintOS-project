#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/synch.h"
#include "threads/thread.h"
#include <list.h>

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/* A structure to hold arguments being passed to start_process. */
struct process_args
{
  char *file_name;
  struct thread *parent;
  struct child *self;
};

#endif /* userprog/process.h */
