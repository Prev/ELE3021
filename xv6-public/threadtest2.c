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
  return 0;
}

void
test1(){
  thread_t threads[NUM_THREAD];
  int i;

  for (i = 0; i < NUM_THREAD; i++){
    if (thread_create(&threads[i], routine1, (void*)i) != 0){
      printf(1, "panic at thread_create\n");
      return;
    }
  }
}

int
main(int argc, char *argv[])
{
  test1();
}
