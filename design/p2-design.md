# Project 2 Design Doc: User Programs

### GROUP

Logan Chester <logan.chester@mail.utoronto.ca>

Jonathan Yao <jonathan.yao@mail.utoronto.ca>

### PRELIMINARIES

Tsung-Han Sher, S. CSCI 350: Pintos Guide. 

## TASK 1: ARGUMENT PASSING

### DATA STRUCTURES

**A1:** Copy here the declaration of each new or changed `struct` or `struct` member, global or static variable, `typedef`, or `enumeration`.  Identify the purpose of each in 25 words or less.

### ALGORITHMS

---

In process.c:

```
/* Create the stack by mapping a zeroed page at the top of user virtual memory and pushing the arguments in argv to it. */
static bool setup_stack (void **esp, char *argv[], int argc);
```

```
/* Parses a raw file name and splits it into tokens to be held within argv. Returns the number of tokens/arguments. */
static int tokenize (const char *file_name, char *argv[], int argc);
```

```
/* Uses the arguments within argv and pushes them to the stack as instructed in the helper doc. */
static void push_args_stack (void **esp, char *argv[], int argc);
```

---

**A2:** Briefly describe how you implemented argument parsing.  How do you arrange for the elements of argv[] to be in the right order? How do you avoid overflowing the stack page?

We implemented argument parsing as follows:
- begin by parsing the raw filename into tokens using the tokenize function
- store the tokens in argv[], store the number of tokens in argc
- pass esp, argv, and argc to setup stack function
- use push_args_stack function to place arguments on stack in their proper order

The proper order of elements is described in the pintos guide cited above. Essentially, each argument in argv is pushed to the stack in reverse order (reverse when compared to the ordering of the raw filename). As this occurs, an array of pointers to these arguments is filled. Then, we ensure word alignment and push null to the stack. Next, the pointers to each of the arguments is pushed (again in reverse order). Finally, we push argv, argc, and a fake return address, in that order. The stack is FILO, everything needs to be pushed to the stack in the reverse order for which it will be popped.

The function push_args_stack ensures that argument passing occurs in the same way (as described above) everytime we load a new file.

We avoid overflowing the stack page by only allowing STACK_ARGS number of arguments within argv.

---

### RATIONALE

---

**A3:** Why does Pintos implement strtok_r() but not strtok()?

Pintos implements strtok_r() and not strtok() because strtok_r() is a reentrant function. It does not use global or static variables and data, and can be interupted and resumed, which is better for handling interupts. Strtok() is not reentrant, therefore if we call it from multiple processes or threads, it can cause unwanted race conditions if it has static variables.

---

**A4:** In Pintos, the kernel separates commands into a executable name and arguments.  In Unix-like systems, the shell does this separation.  Identify at least two advantages of the Unix approach.

The advantages of using the shell to seperate commands into an executable name and arguments is that it simplifies the kernel's responsibility, so that it no longer has to parse and validate the commands, decreasing security issues. It also minimizes the kernel's resource usage by using shells to take care of command parsing.

---

## TASK 2: SYSTEM CALLS

### DATA STRUCTURES

---

**B1:** Copy here the declaration of each new or changed `struct` or `struct` member, global or static variable, `typedef`, or `enumeration`.  Identify the purpose of each in 25 words or less.

In thread.h:
```
struct thread 
{
    ...
    struct thread *parent;                  - Parent thread that spawned this.
    struct semaphore exit_sema;             - Semaphore for waiting on a child.
    struct list children;                   - Stores all children of this thread.
    struct lock children_lock;              - Lock for accessing the children list.
    struct list open_files;                 - All files this thread has open.
    struct file *cur_file;                  - The current file this thread has open.
    int cur_fd;                             - The current max file descriptor that has been assigned by this thread.
    struct child *child_self;               - A pointer to the child structure that represents this thread.
    ...
};
```
```
/* A structure to represent a child thread after it has died. */
struct child
{
  int exit_status;            - The exit status of the thread when it died.
  struct lock lock;           - A lock to ensure synchronized access.
  struct thread *self;        - A pointer to the thread this represents.
  struct list_elem elem;      - List element for children list.
  struct semaphore load_sema; - A semaphore for telling parent when exec has loaded.
  bool loaded;                - Whether the exec loaded successfully or not.
  struct semaphore exit_sema; - A semaphore for waiting on the parent to obtain the exit status.
};
```

In process.h:
```
/* A structure to hold arguments being passed to start_process. */
struct process_args
{
  char *file_name;
  struct thread *parent;
  struct child *self;
};
```

In syscall.h:
```
/* The open_file struct keeps track of the current file being accessed, and its
 * file descriptor. It is used in syscalls where files are being accessed such as
 * open, close, read etc. */
struct open_file
{
  int fd;
  struct file *file;
  struct list_elem elem;
};
```

```
/* Lock used when modifying and accessing files. */
static struct lock filesys_lock;
```

---

**B2:** Describe how file descriptors are associated with open files. Are file descriptors unique within the entire OS or just within a single process?

File descriptors for each open file are unique for each process. When a process opens a file, each "open" syscall returns a new file descriptor, which is incremented each time a file is opened. The file descriptors are unique within each process.

---

### ALGORITHMS

---

**B3:** Describe your code for reading and writing user data from the kernel.

Syscall handler validates the given buffer and page pointer, then calls the syscall function read or write with the three arguments: fd, buffer, and size. They return the number of bytes read/written. They both check if file descriptor is STDOUT or STDIN, otherwise they call find_open_file to find the file with given fd, and read/write the file using the found file and the functions file_write/file_read.

---

**B4:** Suppose a system call causes a full page (4,096 bytes) of data to be copied from user space into the kernel.  What is the least and the greatest possible number of inspections of the page table (e.g. calls to pagedir_get_page()) that might result?  What about for a system call that only copies 2 bytes of data?  Is there room for improvement in these numbers, and how much?

The least number of inspections that can occur is 1. This happens when a segment in memory is 4096 bytes or larger. On the contrary, when a segment in memory is only 1 byte, 4096 inspections will occur because the data being copied needs to be split into 4096 pages each of 1 byte in size.

The process for a system call that only copies 2 bytes of data is similar. 2 is largest, 1 is the smallest. This can be improved by not requiring that each segment be loaded separately, and instead they can share pages if they do not fill the size of the segment in memory on their own. In this case, ```pagedir_get_page()``` would only need to inspect one page if the segment size in memory is 2 and there are two copies of data from the user space that are each 1 byte in size.

---

**B5:** Briefly describe your implementation of the "wait" system call and how it interacts with process termination.

The ```wait``` system call calls ```process_wait```. The ```process_wait``` function waits for the given thread tid to die and returns its exit status. ```process_wait``` then loops through the current thread's child processes using ```find_child``` to find the child process that we are waiting for, obtains the child's exit status, allows it to finish exiting, and returns the exit status of that child.

---

**B6:** Any access to user program memory at a user-specified address can fail due to a bad pointer value.  Such accesses must cause the process to be terminated.  System calls are fraught with such accesses, e.g. a "write" system call requires reading the system call number from the user stack, then each of the call's three arguments, then an arbitrary amount of user memory, and any of these can fail at any point.  This poses a design and error-handling problem: how do you best avoid obscuring the primary function of code in a morass of error-handling?  Furthermore, when an error is detected, how do you ensure that all temporarily allocated resources (locks, buffers, etc.) are freed?  In a few paragraphs, describe the strategy or strategies you adopted for  managing these issues.  Give an example.

To catch the errors that may occur when making a system call, without obscuring the code in error-handling, we first validate the page ptr using a helper function ```validate_page_ptr```, which returns a valid page pointer only if it is not NULL, and the physical address associated with the addr given has a mapping to a virtual address. Each system call goes through this function to be validated. In cases where a buffer is given with the system call, ie. write, and read, the buffer is passed through another helper function that validates the buffer. ```validate_buffer``` validates all the addresses within the given buffer. When the validate helper functions fails to validate either the page pointer or the buffer, the process terminates with exit status -1;

When an error is detected during system calls, if a file system lock is currently being held, it is released before the process is terminated. In the syscall ```open```, if the file is unable to be open or there is an error, the allocated page is freed before the system exits with exit status -1; 

---

### SYNCHRONIZATION

---

**B7:** The "exec" system call returns -1 if loading the new executable fails, so it cannot return before the new executable has completed loading.  How does your code ensure this?  How is the load success/failure status passed back to the thread that calls "exec"?

```exec``` calls ```process_execute``` which in turn calls ```thread_create``` with a new structure as an argument for ```start_process```. This stucture is called ```process_args``` and it holds three arguments to pass ```start_process```: a ```file_name```,a ```parent``` thread, and a child structure ```self```. After calling ```thread_create```, we call ```sema_down``` on the child's ```load_sema``` and wait for the executable to load.

When ```load```, within ```start_process```, returns from loading this executable, we assign the result of the ```load``` operation to the child's field called ```loaded``` and call ```sema_up``` on the ```load_sema``` within the child.

Returning to ```process_execute```, if the load is successful, then we return the ```tid``` given by the call to ```thread_create```. If a load is unsuccessful, we exit having failed (-1).

---

**B8:** Consider parent process P with child process C.  How do you ensure proper synchronization and avoid race conditions when P calls wait(C) before C exits?  After C exits?  How do you ensure that all resources are freed in each case?  How about when P terminates without waiting, before C exits?  After C exits?  Are there any special cases?

When parent P calls ```wait(C)``` before C exits, P will stop and wait for C. 

When P calls ```wait(C)``` after C exits, P will not have a child C, therefore there is no child process to wait for and P will continue. To ensure synchronization and avoid race conditions, a lock, ```children_lock```, is used when removing the child C from the parent P's child list, then ```sema_down``` is called to wait for the child process to finish. The child's ```exit_status``` is obtained before the page resource is freed.
    
When P terminates without waiting, we acquire the ```children_lock``` held by P and remove each ```child``` of P's from its ```children``` list while setting the ```parent``` pointer within the ```child``` to ```NULL```.

---

### RATIONALE 

---

**B9:** Why did you choose to implement access to user memory from the kernel in the way that you did?

We chose to verify the validity of each user-provided pointer before dereferencing it because it is the simplest approach. We understand that checking only that the pointer is below ```PHYS_BASE``` and then handling issues within page fault is faster because it takes advantage of the processors MMU, but altering ```page_fault``` seemed like an over-complication of the issue at hand, and the helpers we created within syscall.c are simple and make the process easy to implement and understand. Plus, when an invalid pointer is provided, our method is faster than handling the issue within ```page-fault```.

---

**B10:** What advantages or disadvantages can you see to your design for file descriptors?

An advantage of our design is the simplicity of the file descriptors associated with each thread. Every time a new file is opened, we increment ```cur_fd``` such that the most recently opened file has the maximum file descriptor among all files opened by the thread. 

A disadvantage is that once a file descriptor has been assigned, it cannot be reassigned following the closing of the file associated with it. An implementation that would have resolved this issue may involve a list of available file descriptors that can be updated every time a file is open and closed. That way, file descriptors would not be wasted as they are in our implementation.

---

**B11:** The default tid_t to pid_t mapping is the identity mapping.  If you changed it, what advantages are there to your approach?

We kept the default tid_t to pid_t mapping, one-to-one.

---

## Survey Questions

In your opinion, was this assignment, or any one of the three problems in it, too easy or too hard?  Did it take too long or too little time?

Did you find that working on a particular part of the assignment gave you greater insight into some aspect of OS design?

Is there some particular fact or hint we should give students in future quarters to help them solve the problems?  Conversely, did you find any of our guidance to be misleading?

Do you have any suggestions for the TAs to more effectively assist students, either for future quarters or the remaining projects?

Any other comments?
