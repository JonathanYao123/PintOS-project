# Project 1 Design Doc: Threads

### GROUP

Logan Chester <logan.chester@mail.utoronto.ca>

Jonathan Yao <jonathan.yao@mail.utoronto.ca>

### PRELIMINARIES

Tsung-Han Sher, S. CSCI 350: Pintos Guide. 

## Task 1: Alarm Clock

### DATA STRUCTURES 
A1: Copy here the declaration of each new or changed 'struct' or 'struct' member, global or static variable, 'typedef', or enumeration. Identify the purpose of each in 25 words or less.

Added to **thread.h**:
```
struct  thread
{
	...
	/* ADDED */
	int64_t  wake_on_tick; /* Thread blocked until system reachs this tick. */
	/* (ADDED) */
	...
};
```

### ALGORITHMS
Instead of busy waiting in *timer_sleep* within **timer.c**, we now have the following in **thread.c**:
```
void thread_sleep (int64_t  ticks) {...}
```
```
void thread_wake (struct thread *t) {...}
```
A2: Briefly describe what happens in a call to timer_sleep(), including the effects of the timer interrupt handler.

In a call to *timer_sleep*, *thread_sleep* is called and it puts the current thread to sleep until the given number of system ticks have passed. It does this by altering the current thread's *wake_on_tick* member and disabling interrupts in order to call *thread_block*. Once *thread_block* has completed, interrupts are re-enabled.

A3: What steps are taken to minimize the amount of time spent in the timer interrupt handler?

*thread_wake* is called on each thread during a timer interrupt (which occurs on each tick). It checks whether a thread is blocked, and if so, should it be blocked. If the system ticks match the given thread's *wake_on_tick* member, *thread_wake* unblocks the thread.


### SYNCHRONIZATION 
A4: How are race conditions avoided when multiple threads call timer_sleep() simultaneously?

Interrupts are disabled while blocking a thread in *thread_sleep* and then immediately re-enabled. When *thread_block* is called in *thread_sleep* with interrupts disabled, the thread won't be pre-empted by another thread, so sleeping the thread then calling schedule() can fully execute with no interruption.

A5: How are race conditions avoided when a timer interrupt occurs during a call to timer_sleep()?

Interrupts are disabled inside *thread_sleep* before *thread_block* is called, so that timer interrupts aren't handled during the function which prevents race conditions between threads where *timer_interrupt* may wake up sleeping threads.

### RATIONALE
A6: Why did you choose this design? In what ways is it superior to another design you considered?

This is not the most efficient implementation as every thread is checked by *thread_wake* on each tick of the system.

In hindsight, it would have made more sense to create a list of sleeping threads (much like there is a list of ready threads) and call *thread_wake* on just these threads on each tick. Better yet, the list could be sorted based on the when a thread is to wake up.

Having said this, the given implementation works and removes the issue of busy waiting. This design choice is simpler than creating a new list and a new data structure for elements within that list as described above.


## Task 2: Priority Scheduling

### DATA STRUCTURES 
B1: Copy here the declaration of each new or changed 'struct' or 'struct' member, global or static variable, 'typedef', or enumeration.  Identify the purpose of each in 25 words or less.

Added to thread.h:
```
struct  thread
{
	...
	/* ADDED */
	int  donated_priority; /* Donated priority. */
	struct  list  holding; /* Locks this thread currently holds. */
	struct  lock  *seeking; /* The lock this thread seeks to acquire. */
	/* (ADDED) */
	...
};
```
*donated_priority* keeps track of the new priority of the thread after a higher priority is donated to it.

*holding* is a list that contains all the locks currently being held by the thread, used in *lock_aquire*, *lock_try_aquire*.

*seeking* is the lock that the thread is waiting for.

Added to synch.h:
```
struct  lock
{
	...
	/* ADDED */
	int  priority; /* Highest priority of a thread seeking this lock. */
	struct  list_elem  holding_elem; /* List element for thread->holding list. */
	/* (ADDED) */
	...
};
```

B2: Explain the data structure used to track priority donation. Use ASCII art to diagram a nested donation. (Alternately, submit a .png file.)


**NOTE:** please see nested-donation.png


### ALGORITHMS
We have a number of functions within **thread.c** to help us accomplish our goal of priority scheduling/donation:
```
/* Sorts the (thread) ready list in non-decreasing fashion 
 * based on each thread's effective priority. */
void thread_sort_ready_list (void)
```
```
/* Returns the given thread's effective priority, that is, 
 * the greater of its donated priority or base priority. */
int thread_effective_priority (struct thread *t)
```
```
/* Returns whether the thread within list element l1 has a
 * greater effective priority than the thread within list element l2. */
bool thread_priority_compare (struct list_elem *l1, 
			      struct list_elem *l2, void *aux)
```
```
/* Removes the provided lock from the given thread's list
 * of held locks. Also updates the given thread's
 * donated priority to be equal to the lock it holds
 * with the greatest priority. */
void thread_holding_list_remove (struct thread *t, struct lock *l)
```

Furthermore, we have a two functions within **synch.c** to help us as well:
```
/* Recursively donates priority to the holder of lock l (if one exists and it
 * has a lower priority than the priority p provided). */
static  void donate_rec (struct  lock *l, int  p)
```
```
/* Used to compare one thread's priority in the waiter queue of a semaphore to
 * another threads priority. This is how the waiter queue will maintain order. */
bool sema_compare_priority (struct  list_elem *l1, struct list_elem *l2, void *aux)
```

B3: How do you ensure that the highest priority thread waiting for a lock, semaphore, or condition variable wakes up first?

The waiters queue in a semaphore is ordered by priority from highest to lowest using thread_priority_compare as described above which ensures that the highest priority thread wakes up.

B4: Describe the sequence of events when a call to lock_acquire() causes a priority donation. How is nested donation handled?

When *lock_acquire* is called from the current thread, the current thread is blocked until the lock becomes available. In order to speed up this process (and prevent situation where a high priority thread is indefinitely blocked by two lower priority threads), priority donation is used. Our algorithm for priority donation is as follows.

If a thread is waiting on a lock held by another thread, it is *seeking* this lock. It will now donate it's priority (assuming it is higher than the other thread's priority). This is a recursive process that can span multiple locks and threads until we reach a base thread that holds an arbitrary lock but is not waiting to acquire another lock. Each thread in this chain is donated a high priority so that they will run and release the required lock(s). This algorithm is implemented within *donate_rec*.

When a thread acquires a lock, the thread is now the *holder* of the lock (see **data structures** above) and the lock is now *held* by the thread (potentially with other locks).

B5: Describe the sequence of events when lock_release() is called on a lock that a higher-priority thread is waiting for.

When *lock_release* is called on a lock that has a higher-priority thread waiting on it, that lock is freed from its *holder* and *sema_up* is called (as locks are implemented using semaphores). *sema_up* unblocks the highest priority *waiter* of this lock and calls *thread_yield*. Now the higher-priority thread will run and acquire the lock.

Furthermore, when a thread releases a lock, it no longer requires the priority donated to it by a seeker of that same lock. It may, however, hold another lock for which a donation as occured. The locks a thread still holds are thus used to determine the donation priority of a thread following the release of a lock. 


### SYNCHRONIZATION 

B6: Describe a potential race in thread_set_priority() and explain how your implementation avoids it.  Can you use a lock to avoid this race?

The potential race is multiple threads setting their priority when the list *ready_list*, containing threads in the ready state, is being sorted. Our implementation of *thread_set_priority* contains a call to *thread_yield*, which forces scheduling, and the ordering of the ready list. Since interrupts are disabled in order to sort the *ready_list* and call *schedule*, this prevents the possible race of multiple threads when setting their priority.


### RATIONALE 

B7: Why did you choose this design?  In what ways is it superior to another design you considered?

We initially considered a design where each thread only tracked the lock it was seeking, not the locks it held. This did not allow a thread to re-calculate its donated priority following the release of a lock. Our current implementation allows for accurate tracking of locks being held by each thread and what effective priority a thread has at each stage of its lifecycle.

There is significant reuse of functions throughout our implementation, especially when calculating effective priority of a thread and comparing priorities of threads. This is a highlight of our design and results in clean and concise code.

One drawback in our design is the use of recursion to complete priority donation. Use of recursion in OS and kernel code can be frowned upon and lead to an overflow of the stack. A simple while loop may have sufficed, but the current implementation highlights the recursive nature of priority donation nicely.

## Task 3: Advanced Scheduling

### DATA STRUCTURES 

C1: Copy here the declaration of each new or changed 'struct' or 'struct' member, global or static variable, 'typedef', or enumeration. Identify the purpose of each in 25 words or less.

Added to thread.h:
```
struct  thread
{
	...
	/* ADDED */
	int  nice; /* Niceness. */
	int  recent_cpu; /* Recent CPU usage. */
	/* (ADDED) */
	...
};
```

### ALGORITHMS

C2: Suppose threads A, B, and C have nice values 0, 1, and 2. Each has a recent_cpu value of 0.

The table below shows the scheduling decision and the priority and recent_cpu values for each thread after each given number of timer ticks:
```
| timer |recent_cpu | priority  | thread |
| ticks | A | B | C | A | B | C | to run |
|-------|---|---|---|---|---|---|--------|
|   0	| 0 | 0 | 0 | 63| 61| 59|    A   |
|   4   | 4 | 0 | 0 | 62| 61| 59|    A   |
|   8   | 8 | 0 | 0 | 61| 61| 59|    B   |
|  12   | 8 | 4 | 0 | 61| 60| 59|    A   |
|  16   | 12| 4 | 0 | 60| 60| 59|    B   |
|  20   | 12| 8 | 0 | 60| 59| 59|    A   |
|  24   | 16| 8 | 0 | 59| 59| 59|    C   |
|  28   | 16| 8 | 4 | 59| 59| 58|    B   |
|  32   | 16| 12| 4 | 59| 58| 58|    A   |
|  36   | 20| 12| 4 | 58| 58| 58|    C   |
```

C3: Did any ambiguities in the scheduler specification make values in the table uncertain?  If so, what rule did you use to resolve them?  Does this match the behavior of your scheduler? 

The ambiguity of the values in the table that could be uncertain is when multiple threads have the same priority. This can be solved with the specification that the scheduler must run them in "round robin" order. Our scheduler has the same behaviour. When a thread yields, it is inserted into into the *ready_list* using *thread_priority_compare* to ensure the order is correct.

C4: How is the way you divided the cost of scheduling between code inside and outside interrupt context likely to affect performance?

Scheduling is done by both *next_thread_to_run* and *thread_tick*. *next_thread_to_run* is called outside interrupt context, and *thread_tick* is called from the timer interrupt handler. Most of the code is in *thread_tick*, such as calls to calculate priority, load avg, and recent cpu. Although calculations aren't done every tick, the checks are done every tick and thus would likely have an impact on the performance.


### RATIONALE 
C5: Briefly critique your design, pointing out advantages and disadvantages in your design choices. If you were to have extra time to work on this part of the project, how might you choose to refine or improve your design?

Our design does not pass the *mlfqs-load-60* and *mlfqs-load-avg* tests, and the disadvantage in our design is using *thread_foreach()* when recalulating priority and recent cpu. Avoiding the use of thread_foreach() and instead using a for loop could improve the load avg. Since *thread_foreach* requires interrupts to be disabled, this causes an increase in the load avg.

C6: The assignment explains arithmetic for fixed-point math in detail, but it leaves it open to you to implement it.  Why did you decide to implement it the way you did?  If you created an abstraction layer for fixed-point math, that is, an abstract data type and/or a set of functions or macros to manipulate fixed-point numbers, why did you do so?  If not, why not?

The arithmetic for the fixed-point operations were implemented as a set of macros in fixed-point.h to manipulate fixed-point numbers. This makes the speed of execution faster and makes it easier to repeatedly use the operations in the calculation of the priority, recent cpu, and load avg. It also keeps the code more readable when calculating these formulas.

## Survey Questions

In your opinion, was this assignment, or any one of the three problems
in it, too easy or too hard?  Did it take too long or too little time?

**Logan:** This project was too hard. I spent more than 40 hours working on it without taking into consideration lectures, readings, and tutorials. The guidance provided to us by the professor and the TAs was not enough of a helping hand in my opinion. The most useful resource for me was the student guide provided on piazza. 

**Jonathan** The project took too much time to finish when taking into consideration the amount of readings, lectures, and tutorial information that we had to sift through.

Did you find that working on a particular part of the assignment gave
you greater insight into some aspect of OS design?

**Logan:** Priority donation and figuring out how to use/implement locks and semaphores provided me with a greater insight into OS design. I found that the advanced scheduler portion was so cryptic and hard to debug that I didn't gain much from trying to figure it out.

**Jonathan** Similar to Logan, the priority donation section is the part that gave me the most insight into OS design.

Is there some particular fact or hint we should give students in
future quarters to help them solve the problems?  Conversely, did you
find any of our guidance to be misleading?

**Logan:** Providing the student pintos guide a little earlier would have been nice, although you did give it to us two weeks before the due date which is very reasonable. You seem to err on the extreme side of caution when answering piazza posts. By being so cautious, you are missing opportunities to help/teach students.

Do you have any suggestions for the TAs to more effectively assist
students, either for future quarters or the remaining projects?

Any other comments?