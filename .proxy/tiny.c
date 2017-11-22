
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 04/2017 - Stanley Zhang <szz@andrew.cmu.edu>
 * Fixed some style issues, stop using csapp functions where not appropriate
 */
#include "csapp.h"
#include <stdbool.h>

#define HOSTLEN 256
#define SERVLEN 8

/* Information about a connected client. */
typedef struct {
    struct sockaddr_in addr;    // Socket address
    socklen_t addrlen;          // Socket address length
    int connfd;                 // Client connection file descriptor
    char host[HOSTLEN];         // Client host
    char serv[SERVLEN];         // Client service (port)
} client_info;

/* URI parsing results. */
typedef enum {
    PARSE_ERROR,
    PARSE_STATIC,
    PARSE_DYNAMIC
} parse_result;

/*
 * read_requesthdrs - read HTTP request headers
 * Returns true if an error occurred, or false otherwise.
 */
bool read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];

    do {
        if (rio_readlineb(rp, buf, MAXLINE) <= 0) {
            return true;
        }

        printf("%s", buf);
    } while(strncmp(buf, "\r\n", sizeof("\r\n")));

    return false;
}


/*
 * parse_uri - parse URI into filename and CGI args
 *
 * uri - The buffer containing URI. Must contain a NUL-terminated string.
 * filename - The buffer into which the filename will be placed.
 * cgiargs - The buffer into which the CGI args will be placed.
 * NOTE: All buffers must hold MAXLINE bytes, and will contain NUL-terminated
 * strings after parsing.
 *
 * Returns the appropriate parse result for the type of request.
 */
parse_result parse_uri(char *uri, char *filename, char *cgiargs) {
    /* Check if the URI contains "cgi-bin" */
    if (strstr(uri, "cgi-bin")) { /* Dynamic content */
        char *args = strchr(uri, '?');  /* Find the CGI args */
        if (!args) {
            *cgiargs = '\0';    /* No CGI args */
        } else {
            /* Format the CGI args */
            if (snprintf(cgiargs, MAXLINE, "%s", args + 1) >= MAXLINE) {
                return PARSE_ERROR; // Overflow!
            }

            *args = '\0';   /* Remove the args from the URI string */
        }

        /* Format the filename */
        if (snprintf(filename, MAXLINE, ".%s", uri) >= MAXLINE) {
            return PARSE_ERROR; // Overflow!
        }

        return PARSE_DYNAMIC;
    }

    /* Static content */
    /* No CGI args */
    *cgiargs = '\0';

    /* Check if the client is requesting a directory */
    bool is_dir = uri[strnlen(uri, MAXLINE) - 1] == '/';

    /* Format the filename; if requesting a directory, use the home file */
    if (snprintf(filename, MAXLINE, ".%s%s",
                uri, is_dir ? "home.html" : "") >= MAXLINE) {
        return PARSE_ERROR; // Overflow!
    }

    return PARSE_STATIC;
}

/*
 * get_filetype - derive file type from file name
 *
 * filename - The file name. Must be a NUL-terminated string.
 * filetype - The buffer in which the file type will be storaged. Must be at
 * least MAXLINE bytes. Will be a NUL-terminated string.
 */
void get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) {
        strcpy(filetype, "text/html");
    } else if (strstr(filename, ".gif")) {
        strcpy(filetype, "image/gif");
    } else if (strstr(filename, ".png")) {
        strcpy(filetype, "image/png");
    } else if (strstr(filename, ".jpg")) {
        strcpy(filetype, "image/jpeg");
    } else {
        strcpy(filetype, "text/plain");
    }
}


/*
 * serve_static - copy a file back to the client
 */
void serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp;
    char filetype[MAXLINE];
    char buf[MAXBUF];
    size_t buflen;

    get_filetype(filename, filetype);

    /* Send response headers to client */
    buflen = snprintf(buf, MAXBUF,
            "HTTP/1.0 200 OK\r\n" \
            "Server: Tiny Web Server\r\n" \
            "Connection: close\r\n" \
            "Content-Length: %d\r\n" \
            "Content-Type: %s\r\n\r\n", \
            filesize, filetype);
    if (buflen >= MAXBUF) {
        return; // Overflow!
    }

    printf("Response headers:\n%s", buf);

    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing static response headers to client\n");
    }


    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);
    if (rio_writen(fd, srcp, filesize) < 0) {
        fprintf(stderr, "Error writing static file \"%s\" to client\n",
                filename);
    }

    Munmap(srcp, filesize);
}

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
void serve_dynamic(int fd, char *filename, char *cgiargs) {
    char buf[MAXLINE];
    size_t buflen;
    char *emptylist[] = { NULL };

    /* Format first part of HTTP response */
    buflen = snprintf(buf, MAXLINE,
            "HTTP/1.0 200 OK\r\n" \
            "Server: Tiny Web Server\r\n");
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    /* Write first part of HTTP response */
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing dynamic response headers to client\n");
        return;
    }

    if (Fork() == 0) { /* Child */
        /* Real server would set all CGI vars here */
        setenv("QUERY_STRING", cgiargs, 1);
        Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */
        Execve(filename, emptylist, environ); /* Run CGI program */
    }
    Wait(NULL); /* Parent waits for and reaps child */
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum,
        char *shortmsg, char *longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    /* Build the HTTP response body */
    bodylen = snprintf(body, MAXBUF,
            "<!DOCTYPE html>\r\n" \
            "<html>\r\n" \
            "<head><title>Tiny Error</title></head>\r\n" \
            "<body bgcolor=\"ffffff\">\r\n" \
            "<h1>%s: %s</h1>\r\n" \
            "<p>%s: %s</p>\r\n" \
            "<hr /><em>The Tiny Web server</em>\r\n" \
            "</body></html>\r\n", \
            errnum, shortmsg, longmsg, cause);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
            "HTTP/1.0 %s %s\r\n" \
            "Content-Type: text/html\r\n" \
            "Content-Length: %zu\r\n\r\n", \
            errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    /* Write the headers */
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing error response headers to client\n");
        return;
    }

    /* Write the body */
    if (rio_writen(fd, body, bodylen) < 0) {
        fprintf(stderr, "Error writing error response body to client\n");
        return;
    }
}

/*
 * serve - handle one HTTP request/response transaction
 */
void serve(client_info *client) {
    // Get some extra info about the client (hostname/port)
    // This is optional, but it's nice to know who's connected
    Getnameinfo((SA *) &client->addr, client->addrlen,
            client->host, sizeof(client->host),
            client->serv, sizeof(client->serv),
            0);
    printf("Accepted connection from %s:%s\n", client->host, client->serv);

    rio_t rio;
    rio_readinitb(&rio, client->connfd);

    /* Read request line */
    char buf[MAXLINE];
    if (rio_readlineb(&rio, buf, MAXLINE) <= 0) {
        return;
    }

    printf("%s", buf);

    /* Parse the request line and check if it's well-formed */
    char method[MAXLINE];
    char uri[MAXLINE];
    char version;

    /* sscanf must parse exactly 3 things for request line to be well-formed */
    /* version must be either HTTP/1.0 or HTTP/1.1 */
    if (sscanf(buf, "%s %s HTTP/1.%c", method, uri, &version) != 3
            || (version != '0' && version != '1')) {
        clienterror(client->connfd, buf, "400", "Bad Request",
                "Tiny received a malformed request");
        return;
    }

    /* Check that the method is GET */
    if (strncmp(method, "GET", sizeof("GET"))) {
        clienterror(client->connfd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
        return;
    }

    /* Check if reading request headers caused an error */
    if (read_requesthdrs(&rio)) {
        return;
    }

    /* Parse URI from GET request */
    char filename[MAXLINE], cgiargs[MAXLINE];
    parse_result result = parse_uri(uri, filename, cgiargs);
    if (result == PARSE_ERROR) {
        clienterror(client->connfd, uri, "400", "Bad Request",
                "Tiny could not parse the request URI");
        return;
    }

    /* Attempt to stat the file */
    struct stat sbuf;
    if (stat(filename, &sbuf) < 0) {
        clienterror(client->connfd, filename, "404", "Not found",
                "Tiny couldn't find this file");
        return;
    }

    if (result == PARSE_STATIC) { /* Serve static content */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
            clienterror(client->connfd, filename, "403", "Forbidden",
                    "Tiny couldn't read the file");
            return;
        }
        serve_static(client->connfd, filename, sbuf.st_size);
    } else { /* Serve dynamic content */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
            clienterror(client->connfd, filename, "403", "Forbidden",
                    "Tiny couldn't run the CGI program");
            return;
        }
        serve_dynamic(client->connfd, filename, cgiargs);
    }
}

int main(int argc, char **argv) {
    int listenfd;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    while (1) {
        /* Allocate space on the stack for client info */
        client_info client_data;
        client_info *client = &client_data;

        /* Initialize the length of the address */
        client->addrlen = sizeof(client->addr);

        /* Accept() will block until a client connects to the port */
        client->connfd = Accept(listenfd,
                (SA *) &client->addr, &client->addrlen);

        /* Connection is established; serve client */
        serve(client);
        Close(client->connfd);
    }
}

