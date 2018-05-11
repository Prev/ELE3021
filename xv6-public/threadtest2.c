#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 10

void*
routine1(void *arg)
{
  int tid = (int) arg;
  int i;
  for (i = 0; i < 10000000; i++){
    if (i % 2000000 == 0){
      printf(1, "%d\n", tid);
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

int
main(int argc, char *argv[])
{
  test1();
  return 0;
}
