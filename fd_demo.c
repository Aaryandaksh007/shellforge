#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
    int fd = open("fd_output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("Before dup2: this goes to the terminal\n");

    dup2(fd, STDOUT_FILENO);

    printf("After dup2: this goes into fd_output.txt\n");

    close(fd);

    return 0;
}
