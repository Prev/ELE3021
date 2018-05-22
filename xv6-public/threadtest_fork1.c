#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 10

void*
forkthread(void *arg)
{
    int pid;
    if ((pid = fork()) == -1) {
        printf(1, "panic at fork in forktest\n");
        exit();
    } else if (pid == 0) {
        printf(1, "child\n");
        exit();
    } else {
        printf(1, "parent\n");
        if (wait() == -1) {
            printf(1, "panic at wait in forktest\n");
            exit();
        }
    }
    thread_exit(0);
}

int
main(int argc, char *argv[])
{
    thread_t threads[NUM_THREAD];
    int i;
    void *retval;
    
    for (i = 0; i < NUM_THREAD; i++) {
        if (thread_create(&threads[i], forkthread, (void*)0) != 0) {
            printf(1, "panic at thread_create\n");
            exit();
        }
    }
    for (i = 0; i < NUM_THREAD; i++) {
        if (thread_join(threads[i], &retval) != 0) {
            printf(1, "panic at thread_join\n");
            exit();
        }
    }
    exit();
}

