/* /dev/random alternative

   just an idea i stole from Terry A. Davis.

   use a bible as "book.txt"
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define BOOK "book.txt"
#define FIFO "random"

int
main(void)
{
    mkfifo(FIFO, 0644);

    struct stat fstats;

    stat(BOOK, &fstats);

    int fd = open(BOOK, O_RDONLY);

    void *map = mmap(0, fstats.st_size, PROT_READ, MAP_SHARED, fd, 0);

    while (1) {
        int status;
        int p = open(FIFO, O_WRONLY);
        pid_t pid = fork();
        if (pid == 0) {
            int bytes = 0;
            srand(time(NULL));
            int start = rand() % fstats.st_size;
            while (bytes != -1) {
                bytes = write(p, &map[start++], 1);
                printf("bytes is %d\n", bytes);
            }
            exit(0);
        } else {
            waitpid(pid, &status, NULL);
            close(p);
        }

    }

    munmap(map, fstats.st_size);
    close(fd);

    return EXIT_SUCCESS;
}
