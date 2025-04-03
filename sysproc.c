#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
extern struct proc* allocproc(void);


int sys_profile_test(void) {
  int pid = fork();
  if(pid == 0) {
    // Child process does work
    for(int i=0; i<1000000; i++);
    exit();
  } else if (pid>0) {
    wait();
  }
  return 0;
}


int sys_custom_fork(void) {
  int start_later_flag, exec_time;
  cprintf("inside custom for");
  if (argint(0, &start_later_flag) < 0 || argint(1, &exec_time) < 0)
    return -1;

  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process
  if ((np = allocproc()) == 0)
    return -1;

  // Copy process state from parent
  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0) {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;
  np->tf->eax = 0; // Return 0 to child

  for (int i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  acquire(&ptable.lock);  // Lock before modifying process state

  if (start_later_flag) {
    np->state = SLEEPING;
    np->start_later = 1;
  } else {
    np->state = RUNNABLE;
    np->start_later = 0;
    np->last_runnable_time = ticks;  // Track waiting from creation
  }

  // Store custom parameters
  np->exec_time = exec_time;
  np->elapsed_ticks = 0;
  np->creation_time = ticks;  // Track when the process was created

  release(&ptable.lock);  // Unlock after modifying process state

  return np->pid;
}




int sys_fork(void)
{
  return fork();  // Default fork behavior
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int sys_wait(void) {
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


int sys_scheduler_start(void) {
  struct proc *p;
  
  cprintf("inside Custom scheduler");
  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->start_later && p->state == SLEEPING) {
      p->state = RUNNABLE;
      p->start_later = 0;  // Reset flag
      p->last_runnable_time = ticks;  // Start waiting time tracking

      // **Reset priority boosting parameters**
      p->priority = PRIORITY_INIT;  // Reset priority
      p->boosted = 0;  // Reset priority boost flag
    }
  }

  release(&ptable.lock);

  return 0;
}




