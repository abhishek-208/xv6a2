#include "spinlock.h"
struct proc* allocproc(void);

// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;
extern int scheduler_started;


//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  int start_later;            // 1 if the process should not start immediately
  int exec_time;              // Execution time (ticks), -1 for indefinite execution
  int elapsed_ticks;          // Count of elapsed execution ticks
  int creation_time;      // When process was created (ticks)
  int exit_time;          // When process exited (ticks)
  int first_run_time;     // When process first ran (ticks)
  int total_wait_time;    // Cumulative time in RUNNABLE state
  int last_runnable_time; // Last time it entered RUNNABLE state
  int context_switches;   // Number of times scheduled in/out
  int is_first_run;       // Flag for first execution

  int initial_priority;  // Initial priority (π_i(0))
  int cpu_ticks;         // Total CPU ticks consumed (C_i(t))
  int waiting_time;      // Total waiting time (W_i(t))
  int priority;      // Dynamic priority (lower is higher priority)
  int last_scheduled_time;  // Time when last scheduled
  
  int boosted;       // Flag to indicate if priority was boosted

  int is_exec_limited;   // 1 if exec_ticks is set (>=0), 0 otherwise


  

};

struct ptable_struct {
  struct spinlock lock;
  struct proc proc[NPROC];
};

extern struct ptable_struct ptable;
// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
