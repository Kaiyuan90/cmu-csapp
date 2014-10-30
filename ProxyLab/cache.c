/*
 * cache.c
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
 
#include "cache.h"
 
 
/*
 * init_cache  
 * 
 * initialize the whole cache structure and return the pointer to
 * the structure.
 */
cache *init_cache() {
    cache *pcache = (cache *)Malloc(sizeof(cache));
    if (pcache == NULL) {
        return NULL;
    }
    
    /* initialize the struct and the semaphore */
    pcache->head = NULL;
    pcache->foot = NULL;
    pcache->size = 0;
    Sem_init(&pcache->read, 0, 1);
    Sem_init(&pcache->write, 0, 1);
    pcache->readcnt = 0;
    return pcache;
}

/*
 * find_in_cache 
 * 
 * look the link list to find match, return the pointer to the item if found
 * return NULL otherwise.
 */
cache_item *find_in_cache(char *cache_id, cache *pcache) {
    cache_item *tmp;
    tmp = pcache->head;
    /* Go through the whole list */
    while (tmp != NULL) {
        if (strcmp(tmp->id, cache_id) == 0) {
            return tmp;
        }
        tmp = tmp->next;
    }
    return NULL;
}

/*
 * insert_item 
 * 
 * create a new cache item and insert it to the back of the linked list. 
 * This is the writer function. return -1 if failed.
 */

int insert_item(char *cache_id, char *content, cache *pcache, int size) {
    /* lock it using write lock, no other could write or read */
    P(&(pcache->write));
    
    /* malloc space for the struct */
    cache_item *new_item = (cache_item *)Malloc(sizeof(cache_item));
    if (new_item == NULL){
        V(&(pcache->write));
        return -1;
    }
    
    /* malloc space for the cache id */
    new_item->id = (char *)Malloc(strlen(cache_id)+1);
    if (new_item->id == NULL) {
        Free(new_item);
        V(&(pcache->write));
        return -1;
    }

    /* malloc space for the cache content */
    new_item->content = (char *)Malloc(size);
    if (new_item->content == NULL) {
        Free(new_item->id);
        Free(new_item);
        V(&(pcache->write));
        return -1;
    }
        
    /* copy data into the item struct */
    strcpy(new_item->id, cache_id);
    memcpy(new_item->content, content, size);
    new_item->size = size;

    /* if the exceeds the max cache size, evict! */
    if ((pcache->size + size) > MAX_CACHE_SIZE) {
        evict_lru(size, pcache);
    }
    /* insert the item into the back */
    new_item->next = NULL;
    if (pcache->foot == NULL) {
        pcache->head = new_item;
        pcache->foot = new_item;
    } else {
        pcache->foot->next = new_item;
        pcache->foot = new_item;
    }
    pcache->size += size;
    /* unlock this section */
    V(&(pcache->write));
    return 1;

}

/*
 * read_from_cache
 * 
 * given the pointer, read the content from the cache into a given buffer.
 * after reading, duplicate a same item and put into the back of the list.
 * return -1 if failed
 */

int read_from_cache(char *cache_id, char *content, cache *pcache) {
    /* lock and update readcnt */
    P(&(pcache->read));
    pcache->readcnt++;
    /* first in lock the write lock*/
    if (pcache->readcnt == 1) {
        P(&(pcache->write));
    }
    V(&pcache->read);
    
    /* look for item from the linked list */
    cache_item *item;
    if ((item = find_in_cache(cache_id, pcache)) == NULL) {
        P(&(pcache->read));
        pcache->readcnt--;
        if (pcache->readcnt == 0) {
            V(&(pcache->write));
        }
        V(&pcache->read);
        return -1;
    }
    
    /* copy the data to given buffer*/
    memcpy(content, item->content, item->size);
    int size = item->size;
    /* lock and update readcnt */
    P(&(pcache->read));
    pcache->readcnt--;
    if (pcache->readcnt == 0) {
        V(&(pcache->write));
    }
    V(&pcache->read);
    
    /* insert a new item to the back of the list */
    insert_item(item->id, item->content, pcache, item->size);

    return size;
}

/*
 * evict_lru
 * 
 * keep evicting the first item until the free size meets our demands. 
 * Using LRU policy.
 */

void evict_lru(int new_size, cache *pcache) {
    /* keep evicting until get enough free size*/
    while ((pcache->size + new_size) > MAX_CACHE_SIZE) {
        cache_item *tmp = pcache->head;
        pcache->head = pcache->head->next;
        pcache->size -= tmp->size;
        /* Free what we allocated*/
        Free(tmp->content);
        Free(tmp->id);
        Free(tmp);
    }
}