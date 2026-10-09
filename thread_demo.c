#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

void *mythread(void *arg)
{
    char *name = (char *)arg;

    for (int i = 1; i <= 5; i++) {
        printf("Thread %s: %d\n", name, i);
        sleep(1);
    }

    return NULL;
}

int main(void)
{
    pthread_t p1, p2;

    printf("main: begin\n");

    pthread_create(&p1, NULL, mythread, "A");
    pthread_create(&p2, NULL, mythread, "B");

    pthread_join(p1, NULL);
    pthread_join(p2, NULL);

    printf("main: end\n");

    return 0;
}
