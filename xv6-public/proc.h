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

// Mode of scheduling per each process (MLFQ or STRIDE)
enum schedmode { MLFQ_MODE, STRIDE_MODE };
//enum schedmode { STRIDE_MODE, MLFQ_MODE };

// Priority of process when using MLFQ scheduling
enum mlfqlv { MLFQ_0, MLFQ_1, MLFQ_2 };

// Time unit of Round Robin of each level of queue in MLFQ
#define MLFQ_0_QUANTUM 1
#define MLFQ_1_QUANTUM 2
#define MLFQ_2_QUANTUM 4

// If queue exceed allotment, it's level would be downgraded.
#define MLFQ_0_ALLOTMENT 5
#define MLFQ_1_ALLOTMENT 10

// Boost all process on mlfq with this frequency
#define MLFQ_BOOSTING_FREQUENCY 100

// Data of process when using Stride scheduling
struct stridedata {
  double pass;
  double stride;
  int cpushare;
};

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

  enum schedmode schedmode;    // Scheduling mode (Default: MLFQ)
  enum mlfqlv mlfqlv;          // Level of MLFQ (Default: Q0)
  int mlfqpri;                 // Priority of process in MLFQ (Process that has lower priority will be excuted in same level)
  int ticknum;                 // Ticknum of MLFQ to calculate quantum and allotment
  struct stridedata stride;    // Stride data structure to run as stride mode
  int isyield;
};


// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
