#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>

int main(void)
{
    int pipefd[2];

    if (pipe(pipefd) == -1) {
        perror("pipe");
        return 1;
    }

    printf("Pipe created:\n");
    printf("Read end  = FD %d\n", pipefd[0]);
    printf("Write end = FD %d\n", pipefd[1]);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {

        /* CHILD */

        close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            exit(1);
        }

        close(pipefd[1]);

        printf("Hello from child through the pipe!\n");
        fflush(stdout);

        exit(0);
    }

    /* PARENT */

    close(pipefd[1]);

    waitpid(pid, NULL, 0);

    char buffer[256];

    ssize_t n = read(pipefd[0], buffer, sizeof(buffer) - 1);

    if (n < 0) {
        perror("read");
        close(pipefd[0]);
        return 1;
    }

    buffer[n] = '\0';

    printf("Parent received: %s", buffer);

    close(pipefd[0]);

    return 0;
}
