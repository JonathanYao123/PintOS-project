#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "filesys/file.h"
#include <list.h>
#include <stdbool.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE -1

typedef int pid_t;

struct open_file
{
  int fd;
  struct file *file;
  struct list_elem elem;
};

struct file_mapping
{
  int id;
  struct file *file;
  struct list_elem elem;
  uint8_t *start;
  size_t page_count;
};

void syscall_init (void);

void halt (void);
void exit (int status);
pid_t exec (const char *cmd_line);
int wait (pid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

/* filesys */
bool chdir (const char *dir);
bool mkdir (const char *dir);
bool readdir (int fd, char *name);
bool isdir (int fd);
int inumber (int fd);

#endif /* userprog/syscall.h */
