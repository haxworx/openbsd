#define _DEFAULT_SOURCE 1
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
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

const char *user_agent_override = NULL;

char *
host_from_url(const char *host)
{
    char *addr = strdup(host);
    char *end;

    char *str = strstr(addr, "http://");
    if (str) {
        addr += strlen("http://");
        end = strchr(addr, '/');
        if (end) {
            *end = '\0';
        }
        return strdup(addr);
    }

    str = strstr(addr, "https://");
    if (str) {
        addr += strlen("https://");
        end = strchr(addr, '/');
        if (end) {
            *end = '\0';
        }
        return strdup(addr);
    }


    return strdup(addr);
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
        } else {
            return strdup("/");
        }
    }

    str = strstr(addr, "https://");
    if (str) {
        str += 8;
        char *p = strchr(str, '/');
        if (p) {
            return strdup(p);
        } else {
            return strdup("/");
        }
    }

    return (p);
}


BIO *
Connect_Ssl(const char *hostname, int port)
{
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();

    BIO *bio;
    char bio_addr[8192];

    snprintf(bio_addr, sizeof(bio_addr), "%s:%d", hostname, port);

    SSL_library_init();

    SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
    SSL *ssl = NULL;

    SSL_CTX_load_verify_locations(ctx, "/etc/ssl/certs", NULL);

    bio = BIO_new_ssl_connect(ctx);
    if (!bio) {
        fail("BIO_new_ssl_connect");
    }

    BIO_get_ssl(bio, &ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
    BIO_set_conn_hostname(bio, bio_addr);

    if (BIO_do_connect(bio) <= 0) {
        fail("BIO_do_connect");
    }

    return (bio);

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

typedef struct _data_cb_t data_cb_t;
struct _data_cb_t {
    void *data;
    int size;
};

typedef int (*callback)(void *data);
 
#define MAX_HEADERS 128

typedef struct _header_t header_t;
struct _header_t {
    char *name;
    char *value;
};

typedef struct _request_t request_t;
struct _request_t {
    int sock;
    BIO *bio;
    char *host;
    char *path;
    int status;
    int len;
    bool connection_ssl;
    char *user_agent;
    header_t *headers[MAX_HEADERS];
    void *data;
    callback callback_data;
};

ssize_t 
Write(request_t *request, char *bytes, size_t len)
{
    if (request->connection_ssl) {
        return BIO_write(request->bio, bytes, len);
    }

    return write(request->sock, bytes, len);
}


ssize_t
Read(request_t *request, char *buf, size_t len)
{
    if (request->connection_ssl) {
        return BIO_read(request->bio, buf, len);
    } 

    return read(request->sock, buf, len);
}


char *
header_value(request_t *request, const char *name)
{
    int i;

    for (i = 0; request->headers[i]; i++) {
        header_t *tmp = request->headers[i];
        if (!tmp) return NULL;
        if (!strcmp(tmp->name, name)) {
            return tmp->value;
        }
    }
    return NULL;
}


void
http_content_get(request_t *request)
{
    char buf[1024];
    int length = 0;
    int bytes;

    char *have_length = header_value(request, "Content-Length");
    if (have_length) {
        length = atoi(have_length);
        request->len = length;
    }

    if (!length) return;

    int total = 0;

    /* start the read by reading one byte */
    Read(request, buf, 1);


    if (!request->callback_data) {
        request->data = calloc(1, length);
    }

    do {
        bytes = Read(request, buf, sizeof(buf));
        unsigned char *pos = (unsigned char *) request->data + total;
        if (request->callback_data) {
            data_cb_t received;
            received.data = pos;
            received.size = bytes;
            request->callback_data(&received);
        } else {
            memcpy(pos, buf, bytes);
        }
        total += bytes; 
    } while (total < length);
}


int
http_headers_get(request_t *request)
{
    int i;

    for (i = 0; i < MAX_HEADERS; i++) {
        request->headers[i] = NULL;
    }

    int bytes = 0;
    int len = 0;
    char buf[4096] = { 0 };

    int idx = 0;

    while (1) {
        len = 0;
        while (buf[len - 1] != '\r' && buf[len] != '\n') {
            bytes = Read(request, &buf[len], 1);
            len += bytes;
        }

        buf[len] = '\0';

        if (strlen(buf) == 2) return (1);

        int count = sscanf(buf, "\nHTTP/1.1 %d", &request->status);
        if (count) continue;
        request->headers[idx] = calloc(1, sizeof(header_t));
        char *start = strchr(buf, '\n');
        if (start) {
            start++; 
        }
        char *end = strchr(start, ':');
        *end = '\0';
        request->headers[idx]->name = strdup(start);

        start = end + 1;
        while (start[0] == ' ') {
            start++;
        }

        end = strchr(start, '\r'); 
        *end = '\0'; 
        request->headers[idx]->value = strdup(start);

        ++idx;

        memset(buf, 0, len);
    }

    return (0);
}

void
url_set_user_agent(request_t *request, const char *string)
{
    if (request->user_agent) free(request->user_agent);
    request->user_agent = strdup(string);
}

request_t *
url_new(const char *url)
{
    request_t *request = calloc(1, sizeof(request_t));

    if (!request->user_agent) {
        request->user_agent = strdup
	("Mozilla/5.0 (Linux; Android 4.0.4; Galaxy Nexus Build/IMM76B) AppleWebKit/535.19 (KHTML, like Gecko) Chrome/18.0.1025.133 Mobile Safari/535.19");
    } 

    if (!strncmp(url, "https://", 8)) {
        request->connection_ssl = true;
    } 
 
    request->host = host_from_url(url);
    request->path = path_from_url(url);

    return request;
}

static int
url_get(request_t *request)
{
    if (request->connection_ssl) {
        request->bio  = Connect_Ssl(request->host, 443);
    } else 
        request->sock = Connect(request->host, 80);

    if (request->bio || request->sock) {
        char query[4096];

        snprintf(query, sizeof(query), "GET /%s HTTP/1.1\r\n"
	    "User-Agent: %s\r\n"
    	    "Host: %s\r\n\r\n", request->path, request->user_agent, request->host);
       
        Write(request, query, strlen(query)); 

        http_headers_get(request);

        http_content_get(request);
     
        return request->status;
    }

    return 0;
}


void
url_finish(request_t *request)
{
    if (request->connection_ssl) {
        BIO_free_all(request->bio); 
    } else if (request->sock >= 0) {
        close(request->sock); 
    }
    if (request->host) free(request->host);
    if (request->path) free(request->path);
    int i;
    for (i = 0; i < MAX_HEADERS; i++) {
        header_t *tmp = request->headers[i];
        if (!tmp) { 
            break;
        }
        free(tmp->name);
        free(tmp->value);
        free(tmp);
    } 
    if (request->user_agent) free(request->user_agent);
    free(request->data);
}

void
url_callback_set(request_t *request, int type, callback func)
{
    request->callback_data = func; 
}

void 
usage(void)
{
    printf("./a.out <url> <file>\n");
    exit(EXIT_FAILURE);
}

int 
data_received_cb(void *data)
{
    data_cb_t *received = data;
    if (!received) return 0;
    printf("size is %d!\n", received->size);
}

int
main(int argc, char **argv)
{
    int i;
    
    if (argc != 3) usage();

    request_t *req = url_new(argv[1]);

    url_set_user_agent(req, "Mozilla 7.0");

/*  this callback works but not with the below example 
    
    req->callback_data = data_received_cb;
*/

    int status = url_get(req);
    if (status != 200) {
        fail("status is not 200!");
    }

    printf("status is %d\n", req->status);

    for (i = 0; req->headers[i]; i++) {
        header_t *tmp = req->headers[i];
        printf("%s -> %s\n", tmp->name, tmp->value);
    }

    int fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        fail("open()");
    }

    write(fd, req->data, req->len);
    close(fd);

    url_finish(req);

    return (EXIT_SUCCESS);
}


