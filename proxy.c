#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAXC 10

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct {
    char buf[MAX_OBJECT_SIZE];
    char uri[MAXLINE];
    int valid;
} cach;

cach cache[MAXC];

sem_t mutex, writer;
int readcnt = 0;

void cacheWrite(char* buf, char* uri)
{
    int idx = -1;
    for (int i = 0; i < MAXC; i++)
    if (!cache[i].valid)
    {
        idx = i;
        break;
    }
    if (idx == -1)
        idx = MAXC - 1;

    P(&writer);
    for (int i = idx; i > 0; i--)
        cache[i] = cache[i-1];
    strcpy(cache[0].buf, buf);
    strcpy(cache[0].uri, uri);
    cache[0].valid = 1;
    V(&writer);
}

void parse_uri(char *uri, char* hostname, char* port, char* path)
{
    char *hostpose = strstr(uri, "//");
    char *portpose = strstr(hostpose + 2, ":");
    int tmp;
    sscanf(portpose + 1, "%d%s", &tmp, path);
    sprintf(port, "%d", tmp);
    *portpose = '\0';
    strcpy(hostname, hostpose + 2);
    return;
}

void build_request(rio_t *rio, char *request, char* hostname, char* port, char* path) {
    sprintf(request, "%s %s HTTP/1.0\r\n", "GET", path);
    char buf[MAXLINE];
    sprintf(request, "%sHost: %s:%s\r\n", request, hostname, port);
    sprintf(request, "%s%s", request, user_agent_hdr);
    sprintf(request, "%s%s", request, "Connection: close\r\n");
    sprintf(request, "%s%s", request, "Proxy-Connection: close\r\n");
    sprintf(request, "%s\r\n", request);
}


void doit(int fd) 
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], request[MAXLINE], URI[MAXLINE];

    rio_t rio, server_rio;

    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    strcpy(URI, uri);
    int idx = -1;
    for(int i = 0; i < MAXC; i ++) 
        if (cache[i].valid && !strcmp(cache[i].uri, uri))
            idx = i;
    if (idx != -1)
    {
        P(&mutex);
        readcnt++;
        if (readcnt == 1)
            P(&writer);
        V(&mutex);

        cach temp = cache[idx];
        for (int i = idx; i > 0; i--)
            cache[i] = cache[i-1];
        cache[0] = temp;

        Rio_writen(fd, cache[0].buf, strlen(cache[0].buf));

        P(&mutex);
        readcnt--;
        if (readcnt == 0)
            V(&writer);
        V(&mutex);
        return;
    }

    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    parse_uri(uri, hostname, port, path);
    build_request(&rio, request, hostname, port, path);

    int serverfd = Open_clientfd(hostname, port);

    Rio_readinitb(&server_rio, serverfd);
    Rio_writen(serverfd, request, strlen(request));

    size_t n;
    memset(buf, 0, sizeof buf);
    while ((n = Rio_readlineb(&server_rio, request, MAXLINE)) != 0)
    {
        Rio_writen(fd, request, n);
        strcat(buf, request);
    }

    cacheWrite(buf, URI);
    Close(serverfd);
}

void* thread(void* vargp)
{
    int connfd = *((int*)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

int main(int argc, char **argv)
{
    int listenfd, *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    Sem_init(&mutex, 0, 1);
    Sem_init(&writer, 0, 1);
    for(int i = 0; i < MAXC; i++)
        cache[i].valid = 0;

    listenfd = Open_listenfd(argv[1]);
    pthread_t tid;
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        Pthread_create(&tid, NULL, thread, connfd);
    }

    printf("%s", user_agent_hdr);
    return 0;
}
