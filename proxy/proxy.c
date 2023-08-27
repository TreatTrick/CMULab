#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_PORT_NUM 5
#define DICT_SIZE 65535

#define DEBUG 1

#if DEBUG
#define DEBUG_PRINT(fmt, ...) fprintf(stderr, "DEBUG: %s:%d: " fmt "\n", __func__, __LINE__,  ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...)
#endif

sem_t r_mutex, w_mutex;
static int readCnt;

typedef struct uri_t{
    char hostname[MAXLINE];
    char port[MAX_PORT_NUM];
    char path[MAXLINE];
} Uri;


typedef struct cache_obj_t{
    char* obj;
    int objsize;
    int count;
} Cache_obj;




// 定义键值对
typedef struct {
    char *key;
    Cache_obj* value;
} KeyValuePair;

// 定义字典
typedef struct dict_t{
    KeyValuePair *data[DICT_SIZE];
    int cache_size;
    int maxCount;
} Dict;

// 初始化字典
void initDict(Dict *dict) {
    dict->cache_size = 0;
    dict->maxCount = 0;
    for (int i = 0; i < DICT_SIZE; i++) {
        dict->data[i] = NULL;
    }
}

// 计算哈希值
int hash(char *key) {
    int hash = 0;
    int n = strlen(key);
    for (int i = 0; i < n; i++) {
        hash = (hash * 31 + key[i]) % DICT_SIZE;
    }
    return hash;
}

// 插入键值对
void insert(Dict *dict, char *key, Cache_obj* value) {
    int index = hash(key);
    KeyValuePair *pair = (KeyValuePair *)malloc(sizeof(KeyValuePair));
    pair->key = key;
    pair->value = value;
    dict->data[index] = pair;
}

// 查找键对应的值
Cache_obj* search(Dict *dict, char *key) {
    int index = hash(key);
    KeyValuePair *pair = dict->data[index];
    if (pair != NULL && strcmp(pair->key, key) == 0) {
        return pair->value;
    } else {
        return NULL;
    }
}

// 移除键值对
void removeKeyValue(Dict *dict, char *key) {
    int index = hash(key);
    KeyValuePair *pair = dict->data[index];
    if (pair != NULL && strcmp(pair->key, key) == 0) {
        Free(pair->key);
        Free(pair->value->obj);
        Free(pair->value);
        Free(pair);
        dict->data[index] = NULL;
    }
}

static Dict dict;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int connfd);
void Close(int connfd);
void parse_uri(Uri *parseuri, char* uri);
void make_request(char* server, Uri* parseuri);
int send_to_server(char* server, Uri* parseuri);
void send_to_client(int proxyfd, int connfd, char* uri);
void *thread(void* vargp);
void saveCache(Cache_obj* obj, char* uri);
void removeCache();
Cache_obj* readCache(char* uri);

int main(int argc, char** argv)
{
    int listenfd, *connfd;
    socklen_t clientlen;
    char hostname[MAXLINE], port[MAXLINE];
    pthread_t tid;
    initDict(&dict);
    readCnt = 0;
    Sem_init(&r_mutex, 0, 1);
    Sem_init(&w_mutex, 0, 1);

    struct sockaddr_storage clientaddr;
    if (argc != 2)
    {
        fprintf(stderr, "usage :%s <port> \n", argv[0]);
        exit(1);
    }
    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection in connfd %d from (%s %s).\n", *connfd, hostname, port);
        Pthread_create(&tid, NULL, thread, connfd);
    }
    return 0;
}

void doit(int connfd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], uricpy[MAXLINE];
    char server[MAXLINE];
    Uri parseduri;
    rio_t rio;
    int proxyfd;
    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    strcpy(uricpy, uri);
    parse_uri(&parseduri, uri);
    printf("parsed uri: hostname:%s, port:%s, path:%s\n", parseduri.hostname, parseduri.port, parseduri.path);

    if(strcasecmp(method, "GET")){
        printf("Proxy does not implement the method\n");
        return;
    }
    if(strstr(version, "HTTP/1.") == NULL){
        printf("Proxy only support the http protocol.\n");
        return;
    }
    printf("uri:%s\n", uricpy);
    Cache_obj* cacheResponse = readCache(uricpy);
    if(cacheResponse != NULL){
        Rio_writen(connfd, cacheResponse->obj, cacheResponse->objsize);
        printf("cache hitted %zu bytes in key %s\n", cacheResponse->objsize, uricpy);
        return;
    }
    make_request(server, &parseduri);
    proxyfd = send_to_server(server, &parseduri);
    if(proxyfd < 0) return;
    send_to_client(proxyfd, connfd, uricpy);
    Close(proxyfd);
}

void parse_uri(Uri *parseuri, char* uri){
    if (strstr(uri, "http://") != uri) {
        fprintf(stderr, "Error: invalid uri!\n");
        exit(0);
    }
    uri += strlen("http://");
    char* c = strstr(uri, ":");
    *c = '\0';
    strcpy(parseuri->hostname, uri);
    uri = c + 1;
    c = strstr(uri, "/");
    *c = '\0';
    strcpy(parseuri->port, uri);
    *c = '/';
    strcpy(parseuri->path, c);
}

void make_request(char* server, Uri* parseuri){
    sprintf(server, "GET %s HTTP/1.0\r\n", parseuri->path);
    sprintf(server, "%sHost: %s:%s\r\n", server, parseuri->hostname, parseuri->port);
    sprintf(server, "%s%s\r\n", server, user_agent_hdr);
    sprintf(server, "%sConnection: close\r\n", server);
    sprintf(server, "%sProxy-Connection: close\r\n", server);
    sprintf(server, "%s\r\n", server);
}

int send_to_server(char* server, Uri* parseuri){
    int proxyfd;
    rio_t rio;
    proxyfd = Open_clientfd(parseuri->hostname, parseuri->port);
    if(proxyfd < 0){
        printf("open connection to server failed.\n");
        return -1;
    }
    Rio_readinitb(&rio, proxyfd);
    Rio_writen(proxyfd, server, MAXLINE);
    return proxyfd;
}

void send_to_client(int proxyfd, int connfd, char* uri){
    char buf[MAXLINE];
    char cacheBuf[MAX_OBJECT_SIZE];
    char* writePointer = cacheBuf;
    memset(cacheBuf, 0, MAX_OBJECT_SIZE);
    rio_t rio;
    size_t n;
    size_t cachesize = 0;
    Rio_readinitb(&rio, proxyfd);
    printf("get data from server:\n");
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) > 0){
        Rio_writen(connfd, buf, n);
        cachesize += n;
        if(cachesize <= MAX_OBJECT_SIZE){
            memcpy((void*)(writePointer), buf, n);
            writePointer += n;
        }
        else{
            printf("Cache size too big\n");
        }    
    }

    if(cachesize > MAX_OBJECT_SIZE)
        return;

    char* Obj = Malloc(sizeof(char) * cachesize);
    memcpy((void*)Obj, cacheBuf, cachesize);
    Cache_obj* cacheObj = Malloc(sizeof(Cache_obj));
    cacheObj->obj = Obj;
    cacheObj->objsize = cachesize;
    cacheObj->count = dict.maxCount + 1;
    printf("cached %zu bytes in key %s\n", cacheObj->objsize, uri);
    saveCache(cacheObj, uri);
}

void *thread(void* vargp){
    int connfd = *((int*)vargp);
    Pthread_detach(pthread_self());
    printf("serving connfd %d in thread %u\n", connfd, pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

void saveCache(Cache_obj* cacheObj, char* uri){
    P(&w_mutex);
    while((MAX_CACHE_SIZE - dict.cache_size) < (cacheObj->objsize)){
        removeCache();
    }
    insert(&dict, uri, cacheObj);
    dict.cache_size += cacheObj->objsize;
    cacheObj->count = dict.maxCount + 1;
    V(&w_mutex);
}

Cache_obj* readCache(char* uri){
    DEBUG_PRINT();
    P(&r_mutex);
    readCnt++;
    if (readCnt == 1) {
        P(&w_mutex);
    }
    V(&r_mutex);
    DEBUG_PRINT();
    Cache_obj* cacheobj = NULL;
    cacheobj = search(&dict, uri);
    DEBUG_PRINT();
    if(cacheobj != NULL){
        DEBUG_PRINT();
        cacheobj->count += 1;
        if(cacheobj->count > dict.maxCount){
            dict.maxCount = cacheobj->count;
        }
    }

    P(&r_mutex);
    readCnt--;
    if (readCnt == 0){
        V(&w_mutex);
    }
    V(&r_mutex);
    return cacheobj;
}

void removeCache(){
    char* key;
    Cache_obj* cacheobj;
    int count;
    int isFirst = 1;

    for (int i = 0; i < DICT_SIZE; i++) {
        KeyValuePair *pair = dict.data[i];
        if (pair != NULL) {
            if(isFirst){
                key = pair->key;
                cacheobj = pair->value;
                count = pair->value->count;
                isFirst = 0;
                continue;
            }
            if((pair->value->count) < count)
            {
                key = pair->key;
                cacheobj = pair->value;
                count = cacheobj->count;
            }
        }
    }

    dict.cache_size -= cacheobj->objsize;
    removeKeyValue(&dict, key);
}