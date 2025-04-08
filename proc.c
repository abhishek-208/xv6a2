#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"


#define NULL ((void*)0)




struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

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
struct proc*
myproc(void) {
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
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->killed=0;  
  p->suspended=0;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
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
	
	
	//cprintf("allocproc: created process with pid %d, killed = %d suspended = %d\n", p->pid,p->killed, p->suspended);

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
//  cprintf("userinit: created init process with pid %d, state %d\n", p->pid, p->state);
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
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  
  release(&ptable.lock);

  return pid;
}


// Recursively clean up all zombie children of a process
void
zumbo_cleanup(struct proc *parent)
{
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->parent == parent) {
      // First recursively clean this child's children
      zumbo_cleanup(p);

      // Now clean this process if it's a zombie
      if (p->killed == 1) {
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->suspended =0;
        p->state = UNUSED;
      }
    }
  }
}




// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);
  
  
  zumbo_cleanup(curproc);   //clean subtree of process 

  // Pass abandoned children if any to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc) //check curproc is parent of someone
    {
      	p->parent = initproc;
      	if(p->state == ZOMBIE)
        wakeup1(initproc);      
      	}
      
     
    }


  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;  //mark current process as zombie to be cleaned up in wait()
  sched();
  panic("zombie exit");
}


// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  struct proc *papa;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited and suspended children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if(p->parent != curproc) //check if curproc is not parent of someone
      continue;
      havekids = 1; //curproc is parent of someone
      papa=p->parent;
      zumbo_cleanup(p);
      
      if(p->state == ZOMBIE){
      pid = p->pid;
      cprintf("i am zombie child of %s and my name is %s with pid %d \n", papa->name,p->name,pid);      
        // Found one.
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->suspended = 0;
        p->state = UNUSED;
        release(&ptable.lock); 
        return pid;
      } 
      
      if(p->suspended)
      {
      	pid = p->pid;
      	cprintf("i am suspended child of %s and my name is %s with pid %d \n", papa->name,p->name,pid); 
      	release(&ptable.lock);     
      	return pid;    
      }
    }
    

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
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
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    
    
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    
      if(p->state != RUNNABLE )       
        continue;
        
     if(p->suspended)  //not schedule suspended processes  
      	continue;    
   
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
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
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
 // cprintf("yielding next process %d (%s)\n", myproc()->pid, myproc()->name);
  sched();
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

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
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
  [ZOMBIE]    "zombie",
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

//--------------------code for control signal handling---------------
void control_signal_handler(int sig)
{

  struct proc *p;
  char *state;
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie",
  };
  
  switch (sig) {
  
    case 1: 
      cprintf("\nCtrl-C is detected by xv6\n");
      
      for (p = ptable.proc; p < &ptable.proc[NPROC] && p->pid !=0 ; p++) 
      {
      	state = states[p->state];
      	//cprintf("Process in case1 %d\n", p->pid);
      	
    	if (p->pid !=1 && p->pid !=2 && p->state != ZOMBIE) 
    	{
    	cprintf("killing process with id and state  %d %s %s\n", p->pid, state, p->name);
    	p->killed=1;  //mark process as killed which will be removed using exit() or wait(). 
    	p->state=ZOMBIE;	
      }
      
      if (p->state == SLEEPING) 
    		{
      		//cprintf("Waking up sh and init manually...\n");
      		p->state = RUNNABLE;
   		}
      
      
      }	  
    break;
    
    case 2:{
    
    cprintf("\nCtrl-B is detected by xv6\n");
      
      
   	struct proc *curp = myproc();
	int suspend_self = 0;
	acquire(&ptable.lock);
	for (p = ptable.proc; p < &ptable.proc[NPROC] && p->pid != 0; p++) {
	
	if (p->state == SLEEPING) 
    		{
      		//cprintf("Waking up sh and init manually...\n");
      		p->state = RUNNABLE;
   		}
   		
  	if (p->pid != 1 && p->pid != 2 && p->suspended==0) 
     	{

    	cprintf("Suspending pid %d (%s)\n", p->pid, p->name);
    
    		// If this is the current process, delay suspension
    		if (p == curp) 
    		{
      		suspend_self = 1;
      		//cprintf("Current process pid %d (%s)\n", p->pid, p->name);
    		} 
    		else 
    		{
      		p->suspended = 1;
      		//cprintf("p->suspended value changed of pid %d (%s)\n", p->pid, p->name);
    		}	
  	}
	}
	// Suspend self
	if (suspend_self) {
  	curp->suspended =1;
  	//cprintf("p->suspended value changed of current pid %d (%s)\n", curp->pid, curp->name);
  	release(&ptable.lock);
  	// leave cpu to schedule other processes i.e init or sh
	}
	break;
    }
  
      
    case 3:  
      cprintf("\nCtrl-F is detected by xv6\n");
      acquire(&ptable.lock);
      struct proc *papa;
      for (struct proc *p = ptable.proc; p < &ptable.proc[NPROC] && p->pid !=0; p++) 
      {
         papa=p->parent;
         	
   		
    	if (p->suspended) 
    	 cprintf("Resuming pid %d (%s) of parent %s\n", p->pid, p->name,papa->name);
    	{
       	p->suspended = 0;  // Resume suspended processes
       	}
       	
       	if (p->state == SLEEPING) 
    		{
      		//cprintf("Waking up sh and init manually...\n");
      		p->state = RUNNABLE;
   		}
      }
      release(&ptable.lock);
     break;
      
    case 4:
      	cprintf("\nCtrl-G is detected by xv6\n");
      
	p= myproc();
	
	if (p->signal_handler != NULL) 
	{
     	cprintf("Invoking myHandler() at address: %p\n", p->signal_handler);   
     	   
      	/*cprintf("Before invoking signal handler:\n");
      	cprintf("p->tf->eip = 0x%x\n", p->tf->eip);
    	cprintf("p->tf->esp = 0x%x\n", p->tf->esp);
    	cprintf("p->signal_handler address = 0x%x\n", (uint)p->signal_handler);*/
  
    	// Inject a call to the handler in user space
    	// Save original instruction pointer
    	p->tf->esp -= 4;
    	*(uint*)p->tf->esp = p->tf->eip;  // push current eip

    	// Redirect execution to handler
    	p->tf->eip = (uint)p->signal_handler;     

    	// Print values of eip and esp after modifying the trap frame              
        //cprintf("After invoking myHandler()\n");
    	} 
    
    	else 
    	{
        cprintf("No valid signal handler found\n");
    	}          
    break;
      
    default:
    break;
  } 
  return; 
}

//--------------------code for control signal handling---------------
