#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

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

int
sys_wait(void)
{
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
  //addr = myproc()->sz;
  if((addr = growproc(n)) < 0)
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

// Yield the cpu to the next process
int sys_yield(void)
{
  voluntary_yield();
  return 0;
}

// Get the level of current process
// ready queue of MLFQ.
// Returns one of the level of MLFQ (0/1/2)
int sys_getlev(void)
{
  return myproc()->mlfq.lev;
}

// Inquires to obtain cpu share (%)
int sys_set_cpu_share(void)
{
  int cpu_share;

  if(argint(0, &cpu_share) < 0)
    return -1;

  return set_cpu_share(cpu_share);
}

// Create Thread
int sys_thread_create(void)
{
  int thread, routine, arg;
  //void* (*routine_p)(void*);

  if(argint(0, &thread) < 0)
    return -1;

  if(argint(1, &routine) < 0)
    return -1;

  if(argint(2, &arg) < 0)
    return -1;

  //routine_p = (void*)routine;
  return thread_create((thread_t*)thread, (void*)routine, (void*)arg);
}

// Exit thread
int sys_thread_exit(void)
{
  int retval;
  
  if(argint(0, &retval) < 0)
    return -1;

  thread_exit((void*)retval);
  return 0;
}

// Join thread
int sys_thread_join(void)
{
  int thread, retval;

  if(argint(0, &thread) < 0)
    return -1;

  if(argint(1, &retval) < 0)
    return -1;
    
  return thread_join((thread_t)thread, (void**)retval);
}

int
sys_gettid(void)
{
  return myproc()->tid;
}


