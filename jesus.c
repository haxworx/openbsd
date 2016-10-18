/* 
   Author: Al Poole <netstar@gmail.com>

   pseudo-/dev/random alternative

   An idea i stole from Terry A. Davis.

   use a bible (ASCII) as "book.txt"
   or whatever.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>

int stop_program = 0;

void 
onsignal(int sig)
{
    printf("STOPPING on child shutdown\n");
    stop_program = 1;
}

void 
usage(void)
{
    printf("./program <fifo> <text file>\n");
    exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
    char *fifoname, *textfile;

    if (argc != 3) {
        usage();
    }
   
    signal(SIGINT, onsignal);

    fifoname = argv[1]; textfile = argv[2];

    if (mkfifo(fifoname, 0644) < 0) {
        fprintf(stderr, "Error: mkfifo()\n");
        exit(EXIT_FAILURE);
    } 

    struct stat fstats;

    stat(textfile, &fstats);

    int fd = open(textfile, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Error: open()\n");
        exit(EXIT_FAILURE);
    }

    void *map = mmap(0, fstats.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == NULL) {
        fprintf(stderr, "Error: mmap()\n");
        exit(EXIT_FAILURE);
    }

    while (!stop_program) {
        if (stop_program) break;
        int status;

        int p = open(fifoname, O_WRONLY | O_CREAT | O_TRUNC);
        if (p == -1) {
            fprintf(stderr, "Error: open()\n");
            exit(EXIT_FAILURE);
        }

        if (stop_program) break;

        pid_t pid = fork();
        if (pid < 0 ) {
            fprintf(stderr, "Error: fork()\n");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            close(fileno(stdout));

            int bytes = 0;

            srand(time(NULL));

            int start = rand() % fstats.st_size;

            while (bytes != -1) {
                if (start == fstats.st_size - 1) {
                    start = 0;
                }

                bytes = write(p, (char *) map + start++, 1);
            }

            exit(0);

        } else {
            waitpid(pid, &status, NULL);
            close(p);
        }
    }

    munmap(map, fstats.st_size);
    close(fd);
    unlink(fifoname);
    
    return EXIT_SUCCESS;
}

