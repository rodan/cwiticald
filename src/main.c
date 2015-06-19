
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "main.h"
#include "fifo.h"

int main() {
    printf("--beginning of program\n");

    fifo_t *fifo;
    int counter = 0;
    pid_t pid = fork();
    fifo = create_fifo(FIFO_SZ);

    if (pid == 0)
    {
        // child process that handles the random data transfer 
        // into the fifo


        //rng_fill();
        int i = 0;
        for (; i < 5; ++i)
        {
            printf("child process: counter=%d\n", ++counter);
        }
    }
    else if (pid > 0)
    {
        // parent process
        while (1) {
        }
    }
    else
    {
        // fork failed
        printf("fork() failed!\n");
        return EXIT_FAILURE;
    }

    printf("--end of program--\n");
    return EXIT_SUCCESS;
}

