#include "csapp.h"
#include <pthread.h>
#include "cache.h"



#define MAX_OBJECT_SIZE 102400
#define MAX_TOKEN_NUM 4
#define MAX_TOKEN_LEN 100
#define PORT_CHAR_NUM 6
#define HOST_CHAR_NUM 50
#define REST_CHAR_NUM 200
#define HOSTLEN 256
#define SERVLEN 8
#define MAX_RESPONSE_SIZE 512000
#define DEBUG 0

static const char *header_user_agent = "Mozilla/5.0"
                                    " (X11; Linux x86_64; rv:45.0)"
                                    " Gecko/20100101 Firefox/45.0";


static const char *request_method_get = "GET";
static const char *request_protocol = "HTTP/1.0";
static const char *header_host_key = "Host:";
static const char *header_ua_key = "User-Agent:";
static const char *header_conn_key = "Connection:";
static const char *header_conn_value = "close";
static const char *header_proxconn_key = "Proxy-Connection:";
// Information about a connected client.
typedef struct {
    struct sockaddr_in addr;    // Socket address
    socklen_t addrlen;          // Socket address length
    int connfd;                 // Client connection file descriptor
    char host[HOSTLEN];         // Client host
    char serv[SERVLEN];         // Client service (port)
} client_info;

/* return -1 on single token length > maxTokenLen
 * return -2 on number of tokens > maxTokens
 */

int tokenize(char *buf, int maxTokens, int maxTokenLen, char tokens[maxTokens][maxTokenLen]){

    int i, curInd = 0, writeInd;
    for(i = 0; i <= maxTokens; i++){
        if(buf[curInd] == '\n' || buf[curInd] == '\r'){
            break;
        }
        if(i == maxTokens){
            // if reaches here, the tokens contained is more than madTokens
            return -2;
        }
        while(buf[curInd] == '\t' || buf[curInd] == ' '){
            curInd++;
        }
        writeInd = 0;
        while(buf[curInd] != '\t' && buf[curInd] != ' ' 
            && buf[curInd] != '\r' && buf[curInd] != '\n' 
            && writeInd < maxTokenLen - 1){
            tokens[i][writeInd++] = buf[curInd++];
        } 
        if(writeInd == maxTokenLen - 1){
            return -1;
        }
        // NULL terminate
        tokens[i][writeInd] = 0;
    }   
    return i;
}

/* return -1: host too long
 * return -2: port too long
 * return -3: uri not starting with //
 */

int parse_uri(char* uri, char* port, char* host, char* rest){
    int i = 0;
    // we are assuming that the url starts with http 
    // forward to the first //
    char *host_start;
    if((host_start = strstr(uri,"//")) != NULL){
        host_start += 2;
        while(*host_start != ':' && *host_start != '/'){
            host[i++] = *host_start++;
            if(i == HOST_CHAR_NUM){
                fprintf(stderr, "host too long\n");
                return -1;
            }
        }
        host[i] = 0;                //the null terminator
        i = 0;
        // specified a port
        if(*host_start == ':'){
            host_start += 1;
            while(*host_start >= '0' && *host_start <= '9'){
                port[i++] = *host_start++;
                if(i == PORT_CHAR_NUM){
                    fprintf(stderr, "port too long\n");
                    return -2;
                }
            }
        }
        // if not specified a port, the port string just contains a 0
        port[i] = 0;
        i = 0;
        // grab the rest
        while(*host_start != 0){
            rest[i++] = *host_start++;
            if(i == REST_CHAR_NUM){
                fprintf(stderr,"rest too long\n");
                return -3;
            }
        } 
        // if directly terminating, still put a '/'
        if(i == 0){
            rest[i++] = '/';
        }
        rest[i] = 0;
        return 0;
    }
    else{
        fprintf(stderr, "uri:%s doesn't contain '//'\n",uri);
        return -4; 
    }
}


/* validate whether this is a valid HTTP request
 * and forward the message
 */

int validate_replace(client_info *client, char *forward_buf,
                 char* host, char*port, char*uri){

    ssize_t len;
    int num_line = 0, num_tokens, has_end = 0, host_appear = 0;
    char buf[MAXLINE];
    char rest[REST_CHAR_NUM];
    rio_t rio;
    char write_buf[MAXLINE];
    char tokens[MAX_TOKEN_NUM][MAX_TOKEN_LEN];

    // Initialize RIO read structure
    rio_readinitb(&rio, client->connfd);

    // Get some extra info about the client (hostname/port)
    // This is optional, but it's nice to know who's connected
    Getnameinfo((SA *) &client->addr, client->addrlen,
            client->host, sizeof(client->host),
            client->serv, sizeof(client->serv),
            0);
    printf("Accepted connection from %s:%s\n", client->host, client->serv);
    
    while((len = rio_readlineb(&rio, buf, MAXLINE)) > 0){
        if(num_line == 0){
            num_tokens = tokenize(buf, MAX_TOKEN_NUM, MAX_TOKEN_LEN, tokens);
            if(num_tokens < 2){
                // the first line should contains at least 2 tokens
                fprintf(stderr, 
               "number of tokens:%d in first line less than 2\n",num_tokens);
                return -1;
            }
            if(strcmp(tokens[0], "GET") != 0){
                fprintf(stderr, 
                  "Currently only support GET method, received %s\n",tokens[0]);
                return -1;
            }
            if(parse_uri(tokens[1], port, host, rest) < 0){
                fprintf(stderr, "Error in parsing uri\n");
            }
            // get the uri    
            strcpy(uri,tokens[1]);
            // if the port not specified, use 80
            if(*port == 0){
                strcpy(port,"80");
            }

            // Fill out the write_buf: the 1st line
            sprintf(forward_buf, "%s %s %s\r\n", 
                request_method_get, rest, request_protocol);

            // Fill out the following line: user agent,connection...
            sprintf(write_buf, "%s %s\r\n", 
                header_ua_key, header_user_agent);
            sprintf(write_buf, "%s%s %s\r\n",
                write_buf, header_conn_key, header_conn_value);
            sprintf(write_buf, "%s%s %s\r\n",
                write_buf, header_proxconn_key, header_conn_value);

        }
        else{
            if(strstr(buf,header_ua_key) != NULL){
                // omit user agent
            }
            else if(strstr(buf,header_conn_key) != NULL){
                // omit connection
            }
            else if(strstr(buf, header_proxconn_key) != NULL){
                // omit proxy connection
            }
            else if(strlen(buf) == 2 && strstr(buf,"\r\n") != NULL){
                has_end = 1;
                // if encounter the last line
                if(!host_appear){
                    // if host header didn't appear, write it
                    sprintf(forward_buf,"%s%s %s\r\n",
                        forward_buf,header_host_key,host);
                }
                sprintf(forward_buf, "%s%s\r\n", forward_buf,write_buf);
                break;
            }
            else{
                // host is included in the header
                if(strstr(buf, header_host_key) != NULL){
                    host_appear = 1;
                    sprintf(forward_buf,"%s%s",forward_buf,buf);
                }
                else{ 
                    sprintf(write_buf, "%s%s", write_buf,buf);
                }
            }
        }
        num_line++;
    }


    if(!has_end){
        fprintf(stderr, "received http request without ending line\n");
        return -1;
    }

    return strlen(forward_buf); 
}

int forward_get(char *host, char *port, char *forward_buf, int num_forward, char *response_buf){
    int client_fd, bytes_response;
    rio_t rio;    
    // Open socket connection to server
    if ((client_fd = open_clientfd(host, port)) < 0) {
        fprintf(stderr, "Error connecting to %s:%s\n", host, port);
        return -1;
    }
    
    // Initialize RIO read structure
    rio_readinitb(&rio, client_fd);

    // Write line to server
    if (rio_writen(client_fd, forward_buf, num_forward) < 0) {
        fprintf(stderr, "Error writing to server\n");
        return -1;
    }
    
    if((bytes_response = rio_readnb(&rio, response_buf, MAX_RESPONSE_SIZE)) < 0){
        fprintf(stderr, "Error reading response from server\n");
        return -1;        
    }

    return bytes_response;

}


void *handle_connect(void *arg){
    
    pthread_detach(pthread_self());    
    
    client_info *client = (client_info *) arg; 

    char forward_buf[MAXLINE];
    char forward_host[HOST_CHAR_NUM];
    char forward_port[PORT_CHAR_NUM];
    char *response_buf;
    int bytes_response, real_write, num_forward_bytes;
    char uri[HOST_CHAR_NUM + REST_CHAR_NUM]; 
   
    cache_block *cache_entry;

    if((num_forward_bytes = 
            validate_replace(client, forward_buf, 
                forward_host, forward_port, uri)) < 0){
        fprintf(stdout, "error parsing request\n");
    }
    else{
        if((cache_entry = cache_exist(uri)) != NULL){
            // if it is cached
            bytes_response = cache_entry->bytes;
            response_buf = cache_entry->buf;
            // Write message back to client
            if ((real_write = rio_writen(client->connfd, response_buf, bytes_response)) != bytes_response) {
                fprintf(stderr, "Error writing to back to client, write %d\n", real_write);
            }
            // unlock the cache_entry
            cache_read_done(cache_entry);
        }
        else{

            // dynamically allocate buffer for storing response
            response_buf = (char *)malloc(MAX_RESPONSE_SIZE);

            if((bytes_response = forward_get(forward_host, forward_port, 
                forward_buf, num_forward_bytes, response_buf)) < 0){
                fprintf(stderr, "error when forwarding and getting response\n");
            }
            else{
                // Write message back to client
                if ((real_write = rio_writen(client->connfd, response_buf, bytes_response)) != bytes_response) {
                    fprintf(stderr, "Error writing to back to client, write %d\n", real_write);
                }
                else{
                    // if response is small, cache it
                    if(bytes_response <= MAX_OBJECT_SIZE){
                        cache_store(uri, response_buf, bytes_response);
                    }
                    else{
                        // if not cached, free the buf
                        free(response_buf);
                    }
                }
            }
        }
    }

    // close the client
    Close(client->connfd);
    free(client);

    return NULL;

}

int main(int argc, char** argv) {

    if(argc != 2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    char *self_port = argv[1];
    int listenfd;
    pthread_t tid;
    
    client_info *client;

    cache_init();

    // Start listening on the given port number
    if((listenfd = Open_listenfd(self_port)) < 0){
        fprintf(stderr,"can not listen on port:%s, errnum:%d\n",self_port,listenfd);
    }
    fprintf(stdout,"listening on port:%s\n",self_port);
    

    while(1){
        // Allocate space on the stack for client info
        client = (client_info *)malloc(sizeof(client_info));

        // Initialize the length of the address
        client->addrlen = sizeof(client->addr);
        
        // Accept() will block until a client connects to the port
        client->connfd = Accept(listenfd, 
                (SA*) &client->addr, &client->addrlen);
        
        pthread_create(&tid, NULL, &handle_connect, client);

    }

    return 0;
}

