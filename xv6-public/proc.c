#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#define MLFQ_MIN_PORTION 20

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

struct {
  struct stridedata stride;  // MLFQ is processed like client in stride, and this struct includes information of it.
  int totalcpu;              // Total percentage of CPU (0~100)
  int hpriority;             // Highest prioirty on MLFQ to implement queues
  int totaltick;             // Total ticknum of MLFQ to exec priority boostring
} mlfqs;


static struct proc *initproc;

int nextpid = 1;
int nexttid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);
void cleanup_thread(struct proc*);

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
  
  p->schedmode = 0;
  p->tid = 0;
  p->master = 0;

  // Init data of stride & mlfq
  memset(&p->stride, 0, sizeof p->stride);
  memset(&p->mlfq, 0, sizeof p->mlfq);
  
  memset(&p->blankvm, 0, sizeof p->blankvm);

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

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return old size on success, -1 on failure.
int
growproc(int n)
{
  uint sz, oldsz;
  struct proc *curproc = myproc();
  struct proc *p;

  acquire(&ptable.lock);

  // Grow up master's size if current process is thread
  p = (curproc->master) ? curproc->master : curproc;

  sz = p->sz;
  oldsz = sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      goto bad;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      goto bad;
  }
  p->sz = sz;
  release(&ptable.lock);

  switchuvm(curproc);
  return oldsz;

bad:
  release(&ptable.lock);
  return -1;
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
  
  // Cause memory spaces is managed on master thread,
  // use master's sz on slave's thread
  if(curproc->tid > 0){
    np->pgdir = copyuvm(curproc->pgdir, curproc->master->sz);
  }else{
    np->pgdir = copyuvm(curproc->pgdir, curproc->sz);
  }

  // Check for error
  if(np->pgdir == 0){
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

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd, slavecnt;
  
  if(curproc == initproc)
    panic("init exiting");
  
  // If curproc is master process and there is slave alive,
  // wait until all slaves are killed
  if(curproc->tid == 0){
    acquire(&ptable.lock);

    for(;;){
      slavecnt = 0;
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->master == curproc){
          // If slave thread is already zombie, clean-up it
          // Else, kill slave and wait for it
          if(p->state == ZOMBIE){
            cleanup_thread(p);

          }else{
            slavecnt++;
            p->killed = 1;
            wakeup1(p);
          }
        }
      }
      if(slavecnt == 0){
        release(&ptable.lock);
        break;
      }
      // Wait for slaves to exit.  (See wakeup1 call in proc_exit.)
      sleep(curproc, &ptable.lock);
    }
  }
  
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

  if(curproc->tid == 0){
    // Parent might be sleeping in wait().
    wakeup1(curproc->parent);
 
    // Reset data of stride on exit only proc is master
    mlfqs.totalcpu -= curproc->stride.cpu_share;

  }else{
    // If master is alive
    if(curproc->master != 0){
      curproc->master->killed = 1;
      wakeup1(curproc->master);
    }
  }

  // Pass abandoned children to init.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent == curproc){
        p->parent = initproc;
        if(p->state == ZOMBIE)
          wakeup1(initproc);
      }
    }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
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

void
mlfq_scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  // Run priority boosting if totaltick equals `MLFQ_BOOSTING_FREQUENCY`
  if (mlfqs.totaltick == MLFQ_BOOSTING_FREQUENCY){
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      p->mlfq.lev = MLFQ_0;
      p->mlfq.priority = 0;
      p->mlfq.ticknum = 0;
    }
    mlfqs.hpriority = 0;
    mlfqs.totaltick = 0;
  }

  // Maximum level is 2.
  enum mlfqlev cur_mlfqlev = MLFQ_2;
  int minpri = 1000000;

  // For saving process to run
  struct proc *sp = 0;

  // Choose process by RR
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != RUNNABLE || p->schedmode != MLFQ_MODE)  
      continue;
    
    // Choose lowest queue (means highest priority) in all proccess
    // To forbid choosing multiple process,
    // run process that has `mininum priority` among same level
    if(p->mlfq.lev < cur_mlfqlev){
      cur_mlfqlev = p->mlfq.lev;
      minpri = p->mlfq.priority;
      sp = p;
    
    }else if(p->mlfq.lev == cur_mlfqlev && p->mlfq.priority < minpri){
      minpri = p->mlfq.priority;
      sp = p;
    }
  }

  // If there a process to run
  if((p = sp)) {
    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;
    p->isyield = 0;

    swtch(&(c->scheduler), p->context);
    switchkvm();

    // Increase ticknum of process and totaltick of MLFQ
    p->mlfq.ticknum++;
    mlfqs.totaltick++;

    c->proc = 0;
    
    // If ticknum of process exceeds allotment,
    // reduce it's priority (downgrade level)
    // Else if ticknum is greater than quantum,
    // set mlfq.priority to highest-value to move backward in current level
    // (similar logic to push_back() of queue ADT)
    switch(p->mlfq.lev) {
      case MLFQ_0 :
        if(p->mlfq.ticknum >= MLFQ_0_ALLOTMENT){
          p->mlfq.lev++;
          p->mlfq.ticknum = 0;
        }else if (p->mlfq.ticknum % MLFQ_0_QUANTUM == 0){
           p->mlfq.priority = ++mlfqs.hpriority;
        }
        break;

      case MLFQ_1 :
        if(p->mlfq.ticknum >= MLFQ_1_ALLOTMENT){
          p->mlfq.lev++;
          p->mlfq.ticknum = 0;
        }else if (p->mlfq.ticknum % MLFQ_1_QUANTUM == 0){
          p->mlfq.priority = ++mlfqs.hpriority;
        }
        break;

      case MLFQ_2 :
        if (p->mlfq.ticknum >= MLFQ_2_QUANTUM){
          p->mlfq.priority = ++mlfqs.hpriority;
        }
        break;
    }
  }    
  // Increase pass of whole mlfq,
  // then go back to schedule function,
  // and compare stride again
  mlfqs.stride.pass += (double)(100l / (double)(100 - mlfqs.totalcpu)); // Stride of MLFQ;
}

// Get current stride of given process.
// The ptable lock must be held.
double
getstride(struct proc* sp)
{
  struct proc *p;
  int numthreads = 0;

  if(sp->schedmode != STRIDE_MODE)
    panic("getstride");

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == sp->pid)
      numthreads++;
  }

  return 100l / (double)sp->stride.cpu_share / numthreads;
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
  struct proc *sp;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
  
    sp = 0; // Selected proc
    double minpass = mlfqs.stride.pass;
    int procnum = 0;

    // Find minpass of all process using stride
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      ++procnum;
      if(p->schedmode == STRIDE_MODE && p->stride.pass < minpass){
        minpass = p->stride.pass;
        sp = p;
      }
    }
    
    // If "sp == 0", it means selected client is MLFQ scheduler
    // Else, run other process that runs in stride mode
    // and has lowest pass (minpass)
    if(sp == 0) {
      mlfq_scheduler();

      // If there is no process, reset pass of whole MLFQ to 0
      if(procnum == 0)
        mlfqs.stride.pass = 0;

    }else if((p=sp)){
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();
      
      p->stride.pass += getstride(p);
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
  sched();
  release(&ptable.lock);
}

// Give up the CPU voluntary by process
void
voluntary_yield(void)
{
  myproc()->isyield = 1;
  yield();
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
    if(p->pid == pid && p->tid == 0){
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
    cprintf("[%d](%d) %s %s", p->pid, p->tid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// Reset pass of all processes using stride scheduling.
// The ptable lock must be held.
void
reset_strides(void)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != RUNNABLE || p->schedmode != STRIDE_MODE)
      continue;
    p->stride.pass = 0;
  }
  mlfqs.stride.pass = 0;
}

// Inquires to obtain cpu share (%)
int
set_cpu_share(int cpu_share)
{
  struct proc *curproc = myproc();
  struct proc *p;

  acquire(&ptable.lock);

  if(mlfqs.totalcpu + cpu_share > 100 - MLFQ_MIN_PORTION){
    release(&ptable.lock);
    return -1;
  }

  mlfqs.totalcpu += cpu_share;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == curproc->pid){
      p->schedmode = STRIDE_MODE;
      p->stride.cpu_share = cpu_share;
      // stride.stride is not set in here.
      // It is calculated in `getstride` function every time when it is used.
    }
  }
  reset_strides();

  release(&ptable.lock);
  return 0;
}


// Create thread on this process
int
thread_create(thread_t* thread, void* (*start_routine)(void *), void* arg)
{
  int i;
  uint sz, sp, vabase;
  pde_t *pgdir;
  struct proc *np;
  struct proc *curproc = myproc();
  struct proc *master = curproc->master ? curproc->master : curproc;

  // Allocate process.
  if((np = allocproc()) == 0){
      return -1;
  }

  --nextpid;
  
  // Set thread-dependant properties
  np->master = master;
  np->pid = master->pid;
  np->tid = nexttid++;

  acquire(&ptable.lock);
  pgdir = master->pgdir;
  
  // If there is blank memory on process, use it.
  // Else, grow vm and give new thread memory located at the top
  if(master->blankvm.size){
    vabase = master->blankvm.data[--master->blankvm.size]; // Pop on stack

  }else{
    vabase = master->sz;
    master->sz += 2*PGSIZE;
  }

  // Allocate two pages for current thread.
  // Make the first inaccessible. Use the second as the user stack for new thread
  if((sz = allocuvm(pgdir, vabase, vabase + 2*PGSIZE)) == 0){
    np->state = UNUSED;  
    return -1;
  }
  //clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  release(&ptable.lock);

  // Copy states
  *np->tf = *master->tf;
  np->schedmode = master->schedmode;

  if(np->schedmode == STRIDE_MODE){
    np->stride.cpu_share = master->stride.cpu_share;
    reset_strides();
  }

  for(i = 0; i < NOFILE; i++)
    if(master->ofile[i])
      np->ofile[i] = filedup(master->ofile[i]);
  np->cwd = idup(master->cwd);

  safestrcpy(np->name, master->name, sizeof(master->name));

  sp = sz - 4;
  *((uint*)sp) = (uint)arg; // argument
  sp -= 4;
  *((uint*)sp) = 0xffffffff; // fake return PC

  np->pgdir = pgdir;
  np->vabase = vabase;
  np->sz = sz;
  np->tf->eip = (uint)start_routine; // entry point of this thread
  np->tf->esp = sp; // set stack pointer

  // Return tid through argument
  *thread = np->tid;

  // Make runnable
  acquire(&ptable.lock);

  np->state = RUNNABLE;
  
  release(&ptable.lock);
  
  return 0;
}

// Terminate the thread
// Return value will be passed when calling thread_join by master process
// Similar logic to `exit()` function 
void 
thread_exit(void* retval)
{
  struct proc *curproc = myproc();
  int fd;

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
  
  // Save retval temporarily
  curproc->tmp_retval = retval;

  // Master process might be sleeping in wait().
  wakeup1(curproc->master);

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for the thread to exit. If that thread
// has already terminated, then returns immediately.
// Also cleaning up the resources allocated to the thread
// such as a page table, allocated memories and stacks.
int
thread_join(thread_t thread, void** retval)
{
  struct proc *p;
  struct proc *curproc = myproc();
  
  // Slave thread cannot call thread_join
  if(curproc->master != 0){
    return -1;
  }

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited slave threads.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->tid != thread)
        continue;
      
      // Only master of the slave thread can call thread_join
      if(p->master != curproc){
        release(&ptable.lock);
        return -1;
      }

      if(p->state == ZOMBIE){
        // Found one.
        *retval = p->tmp_retval;
        cleanup_thread(p);

        release(&ptable.lock);
        return 0;
      }
    }

    if(curproc->killed){
      release(&ptable.lock);
      return -1;
    }
    // Wait for slave thread to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

// Clean up resources of the thread.
// Announce to master that area used by this thread
// is currently blank so other could use it.
// The ptable lock must be held.
void
cleanup_thread(struct proc *p)
{
  kfree(p->kstack);
  p->kstack = 0;

  p->master->blankvm.data[p->master->blankvm.size++] = p->vabase;

  p->pid = 0;
  p->parent = 0;
  p->master = 0;
  p->name[0] = 0;
  p->killed = 0;
  p->state = UNUSED;

  // Deallocate memory area of this thread
  deallocuvm(p->pgdir, p->sz, p->vabase);

}

// Called by `exec()` function.
// Search process with given pid, set killed 1 to
// all processes(and threads) that's pid is given pid,
// except for one process.
void
kill_except(int pid, struct proc* except)
{
  struct proc *p;

  acquire(&ptable.lock);

  if(myproc()->killed){
    release(&ptable.lock);
    return;
  }
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->pid == pid && p != except){
      p->killed = 1;
      p->chan = 0;
      p->state = SLEEPING;
    }
  }
  release(&ptable.lock);
}


/// Called by `exec()` function.
// Search process with given pid, and wake up
// all processes(and threads) that's pid is given pid,
// except for one process. They might call exit() this time
void
wakeup_except(int pid, struct proc* except)
{
  struct proc *p;
  int havekids;

  acquire(&ptable.lock);
  havekids = 0;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->pid == pid && p != except){
      p->state = RUNNABLE;

      // Process `except` inherits original process,
      // so collector of this process should be `except` proc
      if(p->parent){
        p->parent = except;
        havekids = 1;
      }
    }
  }
  release(&ptable.lock);

  if(havekids)
    wait();
}
