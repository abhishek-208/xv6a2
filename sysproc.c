#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
extern struct proc* allocproc(void);



int sys_custom_fork(void) {
  int start_later_flag, exec_time;
  
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

  np->start_later = start_later_flag;
  np->exec_time = exec_time;
  np->elapsed_ticks = 0;

  int pid = np->pid;

  acquire(&ptable.lock);

  if (start_later_flag) {
    np->state = SLEEPING; // Put process to sleep until sys_scheduler_start()
  } else {
    np->state = RUNNABLE; // Start immediately like normal fork
  }

  release(&ptable.lock);

  return pid;
}


int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int sys_wait(void) {
  struct proc *curproc = myproc();
  
  // Allow only init (PID 1) to call wait()
  if (curproc->pid == 1) {
      return wait();
  }
  
  // Forbid other processes from waiting (prevents precedence ordering)
  return -1;
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
  
  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->start_later && p->state == SLEEPING) {
      p->state = RUNNABLE;
      p->start_later = 0; // Reset flag
    }
  }

  release(&ptable.lock);
  return 0;
}
