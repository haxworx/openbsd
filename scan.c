#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <time.h>

void fail(char *msg)
{
    fprintf(stderr, "Error: %s\n", msg);
    exit(EXIT_FAILURE);
}

#define PORTS_MAX 65535

int ports[PORTS_MAX];
int port = 1;

#define CONNECTIONS_MAX  32
#define NUM_THREADS 8

int start_port = 1;
int end_port = 1024;

const char *hostname = NULL;
struct hostent *host;

int timeout = 2;

int
Connect(const char *hostname, int port, int timeout)
{
    int 	    sock;
    struct sockaddr_in host_addr;
    fd_set fds;
    struct timeval tm;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (!sock)
	return (0);

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    tm.tv_sec = timeout;
    tm.tv_usec = 0;

    host_addr.sin_family = AF_INET;
    host_addr.sin_port = htons(port);
    host_addr.sin_addr = *((struct in_addr *) host->h_addr);

    int status = connect(sock, (struct sockaddr *) & host_addr,
			      sizeof(struct sockaddr));
    if (status == -1) {
        if (errno == EINPROGRESS) {
            status = select(sock + 1, &fds, &fds, NULL, &tm);
            if (status <= 0) {
                // timeout 0 or error -1
                close(sock);
                return (0);
            } 
        }
    }

    close(sock);
    // 2 -1 rd or wr ready 
    if (status == 1 || status == 0) {
       return (1);
    } 

    return (0);
}

pthread_mutex_t mutex;

void
scan_ports(void *data)
{
    (void) data;
    int i;
    int start = port;
    int max = start + CONNECTIONS_MAX;
    if (start > end_port) return;

    pthread_mutex_lock(&mutex);
    port += CONNECTIONS_MAX;
    pthread_mutex_unlock(&mutex);

    for (i = start; i < max; i++) {
	ports[i] = Connect(hostname, i, timeout);
    }
}


void 
usage(void)
{
    printf("./a.out -p1-1024 <host>\n");
    exit(EXIT_FAILURE);
}

// non-blocking threaded scan
#define SCAN_GENERIC  0
// generic services single thread
#define SCAN_SERVICES 1


int
main(int argc, char **argv)
{
    pthread_t threads[NUM_THREADS];
    int i = 0;
    time_t start, end;
    int type = SCAN_GENERIC;

    if (argc < 3) usage();
 
    for(i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "-p", 2) && strlen(argv[i]) > 2) {
            char *p = argv[i] + 2;
            if (!p) usage();
            char *end = strrchr(p, '-');
            if(!end) usage();
	    *end = '\0';
            start_port = atoi(p);
            p = end + 1;
            end_port = atoi(p);
            if (end_port == 0) usage();
            port = start_port;
        }
    }

    hostname = argv[2];

    host = gethostbyname(hostname);
    if (host == NULL)
	fail("invalid host");

    start = time(NULL);

    struct servent *s;
    switch(type) {
    /* Start of services scan */
    case SCAN_SERVICES: 
    while ((s = getservent()) != NULL) {
       if (strcmp(s->s_proto, "tcp")) continue;
       int port = htons(s->s_port);
       bool connected = Connect(hostname, port, timeout);
       if (connected) {
           printf("Connected on %7d\n", port);
       }
    }
    break;

    /* Generic scan (threaded and non-blocking) */
    case SCAN_GENERIC:
    pthread_mutex_init(&mutex, NULL);

    while (port <= end_port) {
	for (i = 0; i < NUM_THREADS; i++) 
	    pthread_create(&threads[i], NULL, (void *(*)(void *))&scan_ports, NULL);

	for (i = 0; i < NUM_THREADS; i++) 
	    pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&mutex);
    break;
    };

    end = time(NULL);

    for (i = 0; i < PORTS_MAX; i++) {
	if (ports[i]) {
            struct servent *ent = getservbyport(htons(i), NULL);
            if (ent)
	        printf("Connected on %7d (%s)\n", i, ent->s_name);
            else
                printf("Connected on %7d\n", i); 
	}
    }
    
    printf("scan completed in %d seconds.\n", (int)(end - start));

    return (EXIT_SUCCESS);
}
