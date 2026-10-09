#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

int data = 100;                  // DATA
int bss;                         // BSS
const char *msg = "Hello";       // RODATA

void fun(void)
{
    int local = 20;              // STACK

    printf("Stack  : %p\n", (void *)&local);
}

int main(int argc, char *argv[])
{
    int main_local = 10;         // STACK

    int *heap = malloc(sizeof(int));  // HEAP

    if (heap == NULL) {
        perror("malloc");
        return 1;
    }

    *heap = 500;

    int *map = mmap(
        NULL,
        sizeof(int),
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );

    if (map == MAP_FAILED) {
        perror("mmap");
        free(heap);
        return 1;
    }

    *map = 1000;

    printf("PID    : %d\n", getpid());

    printf("Code   : %p\n", (void *)main);          // TEXT
    printf("ROData : %p\n", (void *)msg);           // RODATA
    printf("Data   : %p\n", (void *)&data);        // DATA
    printf("BSS    : %p\n", (void *)&bss);          // BSS
    printf("Stack  : %p\n", (void *)&main_local);  // STACK

    fun();

    printf("Heap   : %p\n", (void *)heap);          // HEAP
    printf("mmap   : %p\n", (void *)map);           // MMAP

    if (argc > 1)
        printf("argv   : %s\n", argv[1]);
    else
        printf("argv   : <none>\n");

    printf("HOME   : %s\n", getenv("HOME"));

    while (1)
        sleep(10);

    munmap(map, sizeof(int));
    free(heap);

    return 0;
}
