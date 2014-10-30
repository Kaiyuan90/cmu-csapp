/*
 * proxy.c
 *
 * Name: Kaiyuan Tang
 * AndrewID: kaiyuant
 * Description: 
 * This is a simple multi-thread proxy with caching. It can only handle GET
 * method. When the proxy receives a request, it first parse it and look 
 * into the cache to see if there is a cache copy. If not found, the proxy
 * will connect to the remote host and send request for client. The response
 * will be transfer back to client and store a copy into cache if the size is
 * not too large. The cache is maintained as a single linked list. New item is
 * always inserted in the back, and when a item is used, create a new item and 
 * add it to the last. This could seem inefficient, but it greatly reduce the 
 * risk of messing up the whole cache system. Thus, evicting LRU item can 
 * simply evict the head of the list.
 * 
 * How to use: provide an argument as the port you want to use
 * CSAPP lib: modified it so that process will not exit due to error. This 
 * keeps the server from being crash.
 */

#include <stdio.h>
#include <stdlib.h>
#include "csapp.h"
#include <string.h>
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";
static const char *http_version = "HTTP/1.0\r\n";

/* functions */
void *thread(void *arg);
int parse_url(char *url, char *protocol, char *remote_host,
                            char *remote_port, char *uri);
void read_headers(rio_t *rp, char *buf, char *request_headers,
                            char *remote_host, char *remote_port);
int open_clientfd_r(char *hostname, char *port);
int fetch_server(int server_fd, int client_fd, char *cache_id);
int fetch_cache(char *cache_id, int client_fd);

/* Make the cache structure global so that it could be easily accessed*/
cache *pcache = NULL;

int main(int argc, char *argv[])
{
    int listenfd, *connfdp, port;
    socklen_t clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid;
    
    /* ignore SIGPIPE */
    Signal(SIGPIPE, SIG_IGN);
    
    if(argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    
    /* initialize the cache struct*/
    pcache = init_cache();
    
    /* Begin listening on port given*/
    port = atoi(argv[1]);
    listenfd = Open_listenfd(port);
    while(1) {
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    Free(pcache);
    return 0;
}


/*
 * thread
 * 
 * the thread will get request from client and try to fetch data from
 * cache. If failed, it will connect to specified server and send request
 * for user and get response to user and maybe make a copy to cache.
 *
 */
void *thread(void *vargp) {
    /* detach the thread to avoid memory leaks*/
    Pthread_detach(pthread_self());
    int client_fd = *((int *)vargp);
    Free(vargp);
    
    int server_fd = -1;
    rio_t client_rio;
    
    char protocol[MAXLINE];
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char remote_host[MAXLINE], remote_port[MAXLINE], uri[MAXLINE];
    char request_lines[MAXLINE], cache_id[MAXLINE];
    
    Rio_readinitb(&client_rio, client_fd);
    /* read the request line into buf */
    if(rio_readlineb(&client_rio, buf, MAXLINE) == -1) {
        Close(client_fd);
        pthread_exit(NULL);
    }
    
    /* Get request method, url and version and make it cache id*/
    sscanf(buf, "%s %s %s", method, url, version);
    strcpy(cache_id, buf);

    /* parse the request to get key information */
    if (parse_url(url, protocol, remote_host, remote_port, uri) == -1) {
        Close(client_fd);
        fprintf(stderr, "Bad url %s at %lu\n", url, pthread_self());
        pthread_exit(NULL);
    }

    /* generate request line */
    strcpy(request_lines, method);
    strcat(request_lines, " ");
    strcat(request_lines, uri);
    strcat(request_lines, " ");
    strcat(request_lines, http_version);
    
    /* only support GET method. If is GET, get request headers */
    if (strstr(method, "GET") != NULL) {
        read_headers(&client_rio, buf, request_lines, 
                                remote_host, remote_port);
    }
    else {
        Close(client_fd);
        fprintf(stderr, "Only support GET method at %lu\n", pthread_self());
        pthread_exit(NULL);
    }

    /* if found from cache, transfer to client and exit */
    if (fetch_cache(cache_id, client_fd) == 1) {
        Close(client_fd);
        pthread_exit(NULL);
    }
    
    /* not found, connect to remote host */
    if ((server_fd = open_clientfd_r(remote_host, remote_port)) == -1){
        Close(client_fd);
        fprintf(stderr, "Error connecting to remote host:%s at %s\n", 
                                remote_host, remote_port);
        pthread_exit(NULL);
    }
    /* send request for user */
    if (rio_writen(server_fd, request_lines, strlen(request_lines)) == -1) {
        Close(client_fd);
        Close(server_fd);
        fprintf(stderr, "Error writing to remote host:%s at %s\n", 
                                remote_host, remote_port);
        pthread_exit(NULL);
    }
    /* get response */
    if (fetch_server(server_fd, client_fd, cache_id) == -1) {
        Close(client_fd);
        Close(server_fd);
        fprintf(stderr, "Error fetching data from:%s\n", remote_host);
        pthread_exit(NULL);
    }
    
    /* Close fd after using */
    Close(client_fd);
    Close(server_fd);
    return NULL;
}


/*
 * parse_url
 * 
 * given the whole url, parse it to get the http protocol, remote host
 * and port and the uri. return -1 if failed to parse it, 1 if succeed.
 *
 */
int parse_url(char *url, char *protocol, char *remote_host, 
                            char *remote_port, char *uri) {
    char tmp[MAXLINE];
    if (strlen(url) < 1) {
        return -1;
    }

    /* set uri default to "/" and port default to 80*/
    strcpy(uri, "/");
    strcpy(remote_port, "80");
    
    /* parse the url into protocol, host(and port) and uri */
    if (strstr(url, "://") != NULL) {
        sscanf(url, "%[^:]://%[^/]%s", protocol, tmp, uri);
    }
    else {
        sscanf(url, "%[^/]%s", tmp, uri);
    }
    
    /* parse the host-and-port into host and port */
    sscanf(tmp, "%[^:]:%s", remote_host, remote_port);

    return 1;
    
}    

/*
 * read_headers
 * 
 * After parse the first line, continue to get the request headers. This 
 * function will read request headers from client and change some important
 * ones to default ones except for the port. Other headers will be unchanged.
 *
 */    
void read_headers(rio_t *rp, char *buf, char *request_lines, 
                        char *remote_host, char *remote_port) {
    /* first add default ones into the request */
    strcat(request_lines, user_agent_hdr);
    strcat(request_lines, accept_hdr);
    strcat(request_lines, accept_encoding_hdr);
    strcat(request_lines, connection_hdr);
    strcat(request_lines, proxy_conn_hdr);
    while (rio_readlineb(rp, buf, MAXLINE) > 0) {
        /* break if reach the end*/
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }
        /* if meets what we already have, just ignore it */
        if (strstr(buf, "User-Agent:") != NULL) {
            continue;
        } else if (strstr(buf, "Accept:") != NULL) {
            continue;
        } else if (strstr(buf, "Accept-Encoding:") != NULL) {
            continue;
        } else if (strstr(buf, "Connection:") != NULL) {
            continue;
        } else if (strstr(buf, "Proxy Connection:") != NULL) {
            continue;
        }
        /* others shoule be unchanged copied*/
        else {
            strcat(request_lines, buf);
        }
    }
    /* if did not get host header, add one using parsed result*/
    if (strstr(request_lines, "Host: ") == NULL) {
        strcat(request_lines, "Host: ");
        strcat(request_lines, remote_host);
        strcat(request_lines, ":");
        strcat(request_lines, remote_port);
        strcat(request_lines, "\r\n");
    }
    /* add the last symbol which indicating end of headers*/
    strcat(request_lines, "\r\n");
}    

/*
 * open_clientfd_r - thread-safe version of open_clientfd
 * copied from the given file, is thread-safe.
 */
int open_clientfd_r(char *hostname, char *port) {
    int clientfd;
    struct addrinfo *addlist, *p;
    int rv;

    /* Create the socket descriptor */
    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }

    /* Get a list of addrinfo structs */
    if ((rv = getaddrinfo(hostname, port, NULL, &addlist)) != 0) {
        return -1;
    }
  
    /* Walk the list, using each addrinfo to try to connect */
    for (p = addlist; p; p = p->ai_next) {
        if ((p->ai_family == AF_INET)) {
            if (connect(clientfd, p->ai_addr, p->ai_addrlen) == 0) {
                break; /* success */
            }
        }
    } 

    /* Clean up */
    freeaddrinfo(addlist);
    if (!p) { /* all connects failed */
        close(clientfd);
        return -1;
    }
    else { /* one of the connects succeeded */
        return clientfd;
    }
}    
    
/*
 * fetch_server
 * 
 * Fetch response from server and forward it to client. If the response is 
 * smaller than max object size, also cache it. return -1 if failed.
 */
int fetch_server(int server_fd, int client_fd, char *cache_id) {
    char buf[MAXLINE], tmp[MAXLINE];
    char cache[MAX_OBJECT_SIZE]; /* for storing content to cache */
    char *runner;
    runner = cache;
    rio_t server_rio;
    int length = 0;            /* how much data read */
    int size = 0;              /* keep track of the whole size */
    int cache_it = 1;          /* this response should be cached or not */
    
    Rio_readinitb(&server_rio, server_fd);
    /* To get the response size as early as possible to avoid useless memory
     * copy ops, we read the headers separately and try to get the size
	 */
    while ((length = Rio_readlineb(&server_rio, buf, MAXLINE)) > 0) {
        if (rio_writen(client_fd, buf, length) == -1) {
            return -1;
        }
        size += length;
        memcpy(runner, buf, length);    
        runner += length;
        /* get the size from the length header */
        if (strstr(buf, "Content-Length:") != NULL) {
            sscanf(buf, "Content-Length: %s",tmp);
            /* if already know it is too big, do not cache it */
            if (atoi(tmp) > MAX_OBJECT_SIZE) {
                cache_it = 0;
            }
        }
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }
    }
    
    /* read the response body */
    while ((length = Rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
        if (rio_writen(client_fd, buf, length) == -1) {
            return -1;
        }
        size += length;
        /* if whole size exceeds the limit, also do not cache it*/
        if (size >= MAX_OBJECT_SIZE) {
            cache_it = 0;
        }
        /* do it while we still think the response should be cached */
        if (cache_it == 1) {
            memcpy(runner, buf, length);    
            runner += length;
        }
    }
    
    /* if the response is at last should be cached, insert it! */
    if (cache_it == 1) {
        insert_item(cache_id, cache, pcache, size);
    }
    return 1;

}

/*
 * fetch_cache  
 * 
 * Look for item in the cache and if found and successfully fetch data from
 * the cache, return 1.
 */

int fetch_cache(char *cache_id, int client_fd) {
    char content[MAX_OBJECT_SIZE];
    int size;
    /* look for cache and write the cached response into content if found*/
    if ((size = read_from_cache(cache_id, content, pcache)) == -1) {
        return -1;
    }
    
    /* write the content back to client */
    if (rio_writen(client_fd, content, size) == -1) {
        return -1;
    }
    return 1;
}
    
    