#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void tvinit(void) {
  int i;
  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);
  initlock(&tickslock, "time");
}

void idtinit(void) {
  lidt(idt, sizeof(idt));
}

void trap(struct trapframe *tf) {
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno) {
    case T_IRQ0 + IRQ_TIMER:
      if(cpuid() == 0){
        acquire(&tickslock);
        ticks++;
        wakeup(&ticks);
        release(&tickslock);
      }
      lapiceoi();

      struct proc* p = myproc();
      if (p && p->state == RUNNING) {
        // Update priority scheduler metrics
        p->cpu_ticks++;

        // Print once if the process is indefinite
        if (p->is_exec_limited && p->exec_time == -1 && p->is_first_run) {
          cprintf("Process %d ('%s') is running indefinitely.\n", p->pid, p->name);
          p->is_first_run = 0;
        }

        // Handle exec_time-limited processes
        if (p->is_exec_limited && p->exec_time != -1) {
          p->elapsed_ticks++;
          if (p->elapsed_ticks >= p->exec_time) {
            cprintf("Process %d exceeded exec time. Exiting...\n", p->pid);
            p->killed = 1;
          }
        }
      }

      break;

    case T_IRQ0 + IRQ_IDE:
      ideintr();
      lapiceoi();
      break;
    case T_IRQ0 + IRQ_IDE+1:
      break;
    case T_IRQ0 + IRQ_KBD:
      kbdintr();
      lapiceoi();
      break;
    case T_IRQ0 + IRQ_COM1:
      uartintr();
      lapiceoi();
      break;
    case T_IRQ0 + 7:
    case T_IRQ0 + IRQ_SPURIOUS:
      cprintf("cpu%d: spurious interrupt at %x:%x\n",
              cpuid(), tf->cs, tf->eip);
      lapiceoi();
      break;

    default:
      if(myproc() == 0 || (tf->cs&3) == 0){
        // Kernel trap with no process or in kernel mode
        cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
                tf->trapno, cpuid(), tf->eip, rcr2());
        panic("trap");
      }
      // User trap: kill the process
      cprintf("pid %d %s: trap %d err %d on cpu %d "
              "eip 0x%x addr 0x%x--kill proc\n",
              myproc()->pid, myproc()->name, tf->trapno,
              tf->err, cpuid(), tf->eip, rcr2());
      myproc()->killed = 1;
  }

  // Exit if process was marked killed (from any trap)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Yield only once after timer interrupt (handled above)
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Final kill check in case we yielded back
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
