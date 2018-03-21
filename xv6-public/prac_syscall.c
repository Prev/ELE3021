#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"

// Simple system call
int
printk_str(char *str)
{
  cprintf("%s\n", str);
  return 0xABCDABCD;
}


//Wrapper for my_syscall
int
sys_myfunction(void)
{
  char *str;
  //Decode argument using argstr
  if (argstr(0, &str) < 0)
    return -1;
  return printk_str(str);
}



int
getppid(void)
{
  struct proc* p = myproc();
  return p->parent->pid;
}


int
sys_getppid(void)
{
  return getppid();
}
