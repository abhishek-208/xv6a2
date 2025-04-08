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

int
sys_custom_fork(int start_later, int exec_time)
{
  // If normal behavior is expected, fall back to fork()
  if (start_later == 0 && exec_time == -1) {
    return fork();  // behave exactly like normal fork
  }

  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from current process.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  // Copy open files and current working directory.
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  // Set the new fields.
  np->start_later = start_later;
  np->exec_time = exec_time;
  

  // Mark as custom-forked process
  np->is_custom_fork = 1;

  // Decide the initial state.
  if(start_later)
    np->state = SLEEPING;
  else
    np->state = RUNNABLE;

  pid = np->pid;

  // Add to process table.
  acquire(&ptable.lock);
  np->state = (start_later ? SLEEPING : RUNNABLE);
  release(&ptable.lock);

  return pid;
}



int sys_scheduler_start(void) {
  struct proc *p;

  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->start_later && p->state == SLEEPING) {
      p->state = RUNNABLE;
      p->start_later = 0;  // Reset flag
      p->last_runnable_time = ticks;  // Start waiting time tracking

      // Optional: Reset priority boost parameters
      p->priority = PRIORITY_INIT;  // Reset to base priority
      p->boosted = 0;               // Reset priority boost flag (if used)
    }
  }

  scheduler_started = 1;  // Set global flag to allow exec_limited processes

  release(&ptable.lock);

  return 0;
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






