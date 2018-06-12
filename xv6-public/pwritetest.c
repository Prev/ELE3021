#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"

#define NUM_THREAD 10

char *filepath = "myfile";
int fd; // Shared among threads

void
pwritetestmain(void *arg)
{
  int tid = (int) arg;
  int r, off, i;
  char data[256];
  
  printf(1, "Thread #%d is writing\n", tid);

  // Prepare data
  for(i = 0; i < 256; i++)
    data[i] = tid + '0';

  // Set offset
  off = tid * 256;

  // pwrite
  if ((r = pwrite(fd, data, sizeof(data), off)) != sizeof(data)){
    printf(1, "write returned %d : failed\n", r);
    exit();
  }
  
  close(fd);
  thread_exit((void *)0);
}

void
pwritetest()
{
  thread_t threads[NUM_THREAD];
  int i;
  void* retval;
  
  // Open file (file is shared among thread)
  fd = open(filepath, O_CREATE | O_RDWR);

  for(i = 0; i < NUM_THREAD; i++){
    if(thread_create(&threads[i], pwritetestmain, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return;
    }
  }

  for (i = 0; i < NUM_THREAD; i++){
    if (thread_join(threads[i], &retval) != 0){
      printf(1, "panic at thread_join\n");
      return; 
    }
  }
}


int
main(int argc, char *argv[])
{
  printf(1, "1. Start pwrite test\n");
  pwritetest();
  printf(1, "Finished\n");

  exit();
}
