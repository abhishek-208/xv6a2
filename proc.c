#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#define MAX_PROFILER_ENTRIES 64

struct profiler_entry {
  int pid;
  int tat;
  int wt;
  int rt;
  int cs;
};

struct profiler_entry prof_data[MAX_PROFILER_ENTRIES];

int prof_index = 0;



struct ptable_struct ptable;


static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc* myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
struct proc* allocproc(void) {
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  p->creation_time = ticks;      // Global time reference
  p->first_run_time = -1;        // Not yet executed
  p->total_wait_time = 0;
  p->context_switches = 0;
  p->is_first_run = 1;

  // **Initialize priority model**
  p->initial_priority = PRIORITY_INIT;  // From Makefile
  p->priority = PRIORITY_INIT;  // Start with initial priority
  p->cpu_ticks = 0;  // No CPU usage at start
  p->waiting_time = 0;  // No waiting time at start
  p->last_scheduled_time = ticks;  // Initialize scheduling reference

  release(&ptable.lock);

  // Allocate kernel stack
  if ((p->kstack = kalloc()) == 0) {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}



//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];
  p = allocproc();

  p->exec_time = -1;  // Ensure init runs indefinitely

  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void) {
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0) {
      return -1;
  }

  // Copy process state from parent.
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0) {
      kfree(np->kstack);   // Free kernel stack
      np->kstack = 0;
      freevm(np->pgdir);   // Free virtual memory (previously missing)
      np->pgdir = 0;
      np->state = UNUSED;
      return -1;
  }

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork() returns 0 in the child process.
  np->tf->eax = 0;

  // Copy custom scheduling fields
  np->start_later = curproc->start_later;     
  np->exec_time = curproc->exec_time;         
  np->elapsed_ticks = curproc->elapsed_ticks;

  // Duplicate file descriptors safely.
  for (i = 0; i < NOFILE; i++) {
      if (curproc->ofile[i]) {
          np->ofile[i] = filedup(curproc->ofile[i]);
          if (np->ofile[i] == 0) {  // Handle failure
              for (int j = 0; j < i; j++) {
                  fileclose(np->ofile[j]); // Close already duplicated files
                  np->ofile[j] = 0;
              }
              freevm(np->pgdir);
              kfree(np->kstack);
              np->kstack = 0;
              np->state = UNUSED;
              return -1;
          }
      }
  }
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  // Ensure process is marked as RUNNABLE **after everything is initialized**
  acquire(&ptable.lock);
  np->state = RUNNABLE;
  np->last_runnable_time = ticks;
  release(&ptable.lock);

  return pid;
}


// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void) {
  struct proc *p = myproc();
  struct proc *child;
  p->exit_time = ticks;

  // Compute metrics
  int tat = p->exit_time - p->creation_time;
  int wt = p->total_wait_time;
  int rt = p->first_run_time - p->creation_time;
  int cs = p->context_switches;

  // Store in profiler data
  if(prof_index < MAX_PROFILER_ENTRIES) {
    prof_data[prof_index].pid = p->pid;
    prof_data[prof_index].tat = tat;
    prof_data[prof_index].wt = wt;
    prof_data[prof_index].rt = rt;
    prof_data[prof_index].cs = cs;
    prof_index++;
  }

  if (p == initproc)
      panic("init exiting");

  // Close all open files
  for (int i = 0; i < NOFILE; i++) {
      if (p->ofile[i]) {
          fileclose(p->ofile[i]);
          p->ofile[i] = 0;
      }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&ptable.lock);

  wakeup1(p->parent);

  // Reassign orphaned children to init process
  for (child = ptable.proc; child < &ptable.proc[NPROC]; child++) {
      if (child->parent == p) {
          child->parent = initproc;
          if (child->state == ZOMBIE)
              wakeup(initproc);
      }
  }

  // Mark process as ZOMBIE (waiting for parent to reap it)
  p->state = ZOMBIE;
  sched();
  panic("zombie exit");
}


// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void) {
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for (;;) {
    havekids = 0;
    
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      
      if(p->parent != curproc || p->state != ZOMBIE) 
        continue;

      // Print profiler data
      

      if (p->state == ZOMBIE) {   // Found a zombie process, clean it up
        pid = p->pid;
        
        for(int i = 0; i < prof_index; i++) {
          if(prof_data[i].pid == pid) {
            cprintf("PID: %d\nTAT: %d\nWT: %d\nRT: %d\n#CS: %d\n\n\n",
                   prof_data[i].pid, prof_data[i].tat, 
                   prof_data[i].wt, prof_data[i].rt, prof_data[i].cs);
            break;
          }
        }
        

        kfree(p->kstack);  // Free kernel stack
        p->kstack = 0;
        freevm(p->pgdir);  // Free user memory
        
        // Reset process metadata
        safestrcpy(p->name, "", sizeof(p->name));
        p->killed = 0;
        p->state = UNUSED; // Mark process slot as free
        
        release(&ptable.lock);
        return pid;
      }
    }

    // If no children exist or parent was killed, return -1
    if (!havekids || curproc->killed) {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit
    sleep(curproc, &ptable.lock);
  }
}


//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void scheduler(void) {
  struct proc *p;
  struct proc *highest_pri_proc;
  struct cpu *c = mycpu();
  int max_priority, current_pri;
  int selected_pid;

  c->proc = 0;

  for (;;) {
    sti();
    acquire(&ptable.lock);

    // Update waiting time for all RUNNABLE processes
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->state == RUNNABLE) {
        p->waiting_time = ticks - p->last_scheduled_time;
      }
    }

    // Find the process with the highest priority
    highest_pri_proc = 0;
    max_priority = -1;
    selected_pid = -1;

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->state != RUNNABLE)
        continue;

      // Calculate dynamic priority π_i(t)
      current_pri = p->initial_priority - (ALPHA * p->cpu_ticks) + (BETA * p->waiting_time);

      // Select process with the highest priority (break ties using the lowest PID)
      if ((current_pri > max_priority) || 
          (current_pri == max_priority && p->pid < selected_pid)) {
        max_priority = current_pri;
        highest_pri_proc = p;
        selected_pid = p->pid;
      }
    }

    if (highest_pri_proc != 0) {
      // If first run, track the first run time
      if (highest_pri_proc->is_first_run) {
        highest_pri_proc->first_run_time = ticks;
        highest_pri_proc->is_first_run = 0;
      }

      // Switch to the selected process
      c->proc = highest_pri_proc;
      switchuvm(highest_pri_proc);
      highest_pri_proc->state = RUNNING;
      highest_pri_proc->context_switches++;

      // Update scheduling reference
      highest_pri_proc->last_scheduled_time = ticks;

      // Perform context switch
      swtch(&(c->scheduler), highest_pri_proc->context);
      switchkvm();

      // If still runnable, update last runnable time
      if (highest_pri_proc->state == RUNNABLE) {
        highest_pri_proc->last_scheduled_time = ticks;
      }

      // Update CPU usage ticks for running process
      highest_pri_proc->cpu_ticks++;

      // Handle process termination if execution time is exceeded
      if (highest_pri_proc->exec_time > 0 && 
          highest_pri_proc->elapsed_ticks++ >= highest_pri_proc->exec_time) {
        highest_pri_proc->killed = 1;
      }

      c->proc = 0;
    }

    release(&ptable.lock);
  }
}


// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  
  acquire(&ptable.lock);
  
  // Update scheduling metrics before yielding
  p->context_switches++;          // Track context switch
  p->state = RUNNABLE;            // Change state
  p->last_runnable_time = ticks;  // Reset waiting timer
  
  sched();                        // Enter scheduler
  
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Acquire ptable.lock if necessary
  if(lk != &ptable.lock){
    acquire(&ptable.lock);
    release(lk);
  }

  // Go to sleep
  p->chan = chan;
  p->state = SLEEPING;
  sched();

  // Cleanup after waking up
  p->chan = 0;

  // Reacquire original lock
  if(lk != &ptable.lock){
    release(&ptable.lock);
    acquire(lk);
  }

  // When process leaves RUNNABLE (in scheduler())
  if(p->state == RUNNABLE) {
    p->total_wait_time += (ticks - p->last_runnable_time);
  }
}


//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == SLEEPING && p->chan == chan) {
      // Wake up the process and update scheduling metrics
      p->state = RUNNABLE;
      p->last_runnable_time = ticks;  // Track when it becomes runnable
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
