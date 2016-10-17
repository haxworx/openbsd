#define _DEFAULT_SOURCE 1
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

#define h_addr h_addr_list[0]

void fail(char *msg)
{
    fprintf(stderr, "Error: %s\n", msg);
    exit(EXIT_FAILURE);
}

char *
host_from_url(const char *host)
{
    char *addr = strdup(host);
    char *end;

    char *str = strstr(addr, "http://");
    if (str) {
        addr += strlen("http://");
        end = strchr(addr, '/');
        *end = '\0';
        return strdup(addr);
    }

    str = strstr(addr, "https://");
    if (str) {
        addr += strlen("https://");
        end = strchr(addr, '/');
        *end = '\0';
        return strdup(addr);
    }

    return (NULL);
}

char *
path_from_url(const char *path)
{
    if (path == NULL) return (NULL);

    char *addr = strdup(path);
    char *p = addr;

    if (!p) {
        return (NULL);
    }

    char *str = strstr(addr, "http://");
    if (str) {
        str += 7;
        char *p = strchr(str, '/');
        if (p) {
            return strdup(p);
        }
    }

    str = strstr(addr, "https://");
    if (str) {
        str += 8;
        char *p = strchr(str, '/');
        if (p) {
            return strdup(p);
        }
    }

    return (p);
}


int
Connect(const char *hostname, int port)
{
    int 	    sock;
    struct sockaddr_in host_addr;
    struct hostent *host;
    
    host = gethostbyname(hostname);
    if (host == NULL)
	fail("invalid host");

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (!sock)
	return (0);

    host_addr.sin_family = AF_INET;
    host_addr.sin_port = htons(port);
    host_addr.sin_addr = *((struct in_addr *) host->h_addr);

    int status = connect(sock, (struct sockaddr *) & host_addr,
			      sizeof(struct sockaddr));
    if (status == 0) {
       return (sock);
    } 

    return (0);
}

#define MAX_HEADERS 128

typedef struct _header_t header_t;
struct _header_t {
    char *name;
    char *value;
};

typedef struct _url_t url_t;
struct _url_t {
    int sock;
    char *host;
    char *path;
    int status;
    int len;
    header_t *headers[MAX_HEADERS];
    void *data;
};


char *
header_value(url_t *request, const char *name)
{
    int i;

    for (i = 0; i < MAX_HEADERS; i++) {
        header_t *tmp = request->headers[i];
        if (!tmp) return NULL;
        if (!strcmp(tmp->name, name)) {
            return tmp->value;
        }
    }
    return NULL;
}

void
http_content_get(url_t *conn)
{
    char buf[1024];
    int length;
    char *have_length = header_value(conn, "Content-Length");
    if (have_length) {
        length = atoi(have_length);
        conn->len = length;
    }
    if (!length) return;
    int total = 0;
    if (!length) return;

    int count = 1;
    char *data = calloc(1, sizeof(buf)); 

    do {
        int bytes = read(conn->sock, buf, sizeof(buf));
        strncat(data, buf, bytes);
// XXX broken
        total += bytes; 
        count++;
        data = realloc(data, sizeof(buf) * count);
    } while (total < length);

    conn->data = calloc(1, length);
    memcpy(conn->data, data, length);
}

int
http_headers_get(url_t *conn)
{
    int i;

    for (i = 0; i < MAX_HEADERS; i++) {
        conn->headers[i] = NULL;
    }

    int bytes = 0;
    int len = 0;
    char buf[4096] = { 0 };

    int idx = 0;

    while (1) {
        len = 0;
        while (buf[len - 1] != '\r' && buf[len] != '\n') {
            bytes = read(conn->sock, &buf[len], 1);
            len += bytes;
        }

        buf[len] = '\0';

        if (strlen(buf) == 2) return (1);

        int count = sscanf(buf, "\nHTTP/1.1 %d", &conn->status);
        if (count) continue;
        conn->headers[idx] = calloc(1, sizeof(header_t));
        char *start = strchr(buf, '\n');
        if (start) {
            start++; 
        }
        char *end = strchr(start, ':');
        *end = '\0';
        conn->headers[idx]->name = strdup(start);

        start = end + 1;
        while (start[0] == ' ') {
            start++;
        }

        end = strchr(start, '\r'); 
        *end = '\0'; 
        conn->headers[idx]->value = strdup(start);

        ++idx;

        memset(buf, 0, len);
    }

    return (0);
}


url_t *
url_get(const char *url)
{
    url_t *request = calloc(1, sizeof(url_t));

    request->host = host_from_url(url);
    request->path = path_from_url(url);
 
    request->sock = Connect(request->host, 80);
    if (request->sock) {
        char query[4096];

        snprintf(query, sizeof(query), "GET /%s HTTP/1.1\r\n"
    	    "Host: %s\r\n\r\n", request->path, request->host);
        write(request->sock, query, strlen(query));

        http_headers_get(request);
        int i;

        for (i = 0; request->headers[i] != NULL; i++) {
            printf("name is %s\n", request->headers[i]->name);
        }

        http_content_get(request);
     
        return request;
    }

    return NULL;
}

void
url_finish(url_t *url)
{
    close(url->sock);
    if (url->host) free(url->host);
    if (url->path) free(url->path);
    int i;
    for (i = 0; i < MAX_HEADERS; i++) {
        header_t *tmp = url->headers[i];
        if (!tmp) { 
            break;
        }
        free(tmp->name);
        free(tmp->value);
        free(tmp);
    } 
}

void 
usage(void)
{
    printf("./a.out <url>\n");
    exit(EXIT_FAILURE);
}


int
main(int argc, char **argv)
{
    if (argc != 2) usage();

    url_t *req = url_get(argv[1]);
    if (req->status != 200) {
        fail("status is not 200!");
    }

    int i;

    printf("status is %d\n", req->status);

    for (i = 0; req->headers[i]; i++) {
        header_t *tmp = req->headers[i];
        printf("%s -> %s\n", tmp->name, tmp->value);
    }

    int fd = open("test.edj", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    unsigned char *data = req->data;
    write(fd, req->data, req->len);
    close(fd);

    url_finish(req);

    return (EXIT_SUCCESS);
}

