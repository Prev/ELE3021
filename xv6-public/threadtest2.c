#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 10

void*
routine1(void *arg)
{
  int tid = (int) arg;
  int i;
  for (i = 0; i < 50000000; i++){
    if (i % 10000000 == 0){
      printf(1, "%d ", tid);
    }
  }
 thread_exit((void *)(tid+1));
}

void
test1(){
  thread_t threads[NUM_THREAD];
  int i;
  void *retval;
  
  for (i = 0; i < NUM_THREAD; i++){
    if (thread_create(&threads[i], routine1, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return;
    }
  }

  for (i = 0; i < NUM_THREAD; i++){
    if (thread_join(threads[i], &retval) != 0){
      printf(1, "panic at thread_join\n");
      return;
    }

    if ((int)retval != i+1){
      printf(1, "panic at thread_join (wrong retval)\n");
      printf(1, "Expected: %d, Real: %d\n", i+1, (int)retval);
      return;
    }
  }
  printf(1, "Test 1 is done!\n");
}



void*
routine2(void *arg)
{
  int pid;

  pid = fork();

  if(pid == 0){
    printf(1, "[%d(%d)] I'm child process of thread! amazing!\n", getpid(), gettid());
    sleep(100);
    exit();

  }else if(pid > 0){
    printf(1, "[%d(%d)] I'm thread. I made one child, and waiting for it.\n", getpid(), gettid());
    wait();
    printf(1, "[%d(%d)] Child is exit\n", getpid(), gettid());

    thread_exit((void *)2000);
  }

  return (void *)-1;
}


void
test2()
{
  thread_t mthread;
  void *retval;

  printf(1, "[%d(%d)] Test2: create thread\n", getpid(), gettid());
  thread_create(&mthread, routine2, (void*)1000);
  
  thread_join(mthread, &retval);
  printf(1, "[%d(%d)] Result on main thread: %d\n", getpid(), gettid(), (int)retval);
}



void*
routine3(void *arg)
{
  while(1) {}
  return (void *)-1;
}


void
test3()
{
  /*thread_t mthread;
  thread_create(&mthread, routine3, (void*)1000);
  
  sleep(10);

  exit();*/
  int pid, i=0;

  pid = fork();

  if(pid == 0){
    while(i < 100) {
      i += 1;
      sleep(10);
    }
    printf(1, "fin!\n");
    exit();

  }else if(pid > 0){
    sleep(10);
    wait();
    exit();
  }
}




int
main(int argc, char *argv[])
{
  printf(1, "===========Test1===========\n");
  test1();
  
  printf(1, "===========Test2===========\n");
  test2();
  
  printf(1, "===========Test3===========\n");
  test3();

  printf(1, "All tests are done\n");
  
  exit();
}
