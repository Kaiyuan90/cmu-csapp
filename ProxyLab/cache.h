/*
 * cache.h
 *
 * Name: Kaiyuan Tang
 * AndrewID: kaiyuant
 *
 * this is the cache for the tiny proxy, it could search the cache and 
 * forward back the cached response. And the eviction policy is LRU.
 * The cache is maintained as a single linked list. New item is always 
 * inserted in the back, and when a item is used, create a new item and
 * add it to the last. This could seem inefficient, but it greatly reduce 
 * the risk of messing up the whole cache system. As all operations are 
 * on the front or the back. And the result shows that it is indeed 
 * a better solution than manipulating the list ops say: deleting
 * it and insert back. And repeatly request for one small content is rare. 
 * Evicting LRU item can simply evict the head of the list.
 */
 
#include "csapp.h"
#include <string.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
    
/* struct for cache item*/
typedef struct cache_item {
    char *id;                  /* id of the cache block */
    struct cache_item *next;   /* pointer to the next cache block*/
    void *content;             /* cached content */
    int size;                  /* size of the content */
} cache_item;

/* struct for the whole linked list*/
typedef struct cache {
    cache_item *head;          /* first one of the list */
    cache_item *foot;          /* last one of the list */
    int size;                  /* whole size used */
    sem_t read;                /* semaphore for read */
    sem_t write;               /* semaphore for write */
    int readcnt;               /* how many thread are reading*/
} cache;

/* functions*/
cache *init_cache();
cache_item *find_in_cache(char *cache_id, cache *pcache);
int insert_item(char *cache_id, char *content, cache *pcache, int size);
int read_from_cache(char *cache_id, char *content, cache *pcache);
void evict_lru(int new_size, cache *pcache);