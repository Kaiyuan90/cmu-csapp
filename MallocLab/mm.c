/*
 * mm.c
 *
 * Name: Kaiyuan Tang
 * ANDREWID: kaiyuant
 * 
 * Description:
 * In this lab, I implemented segregated free lists to keep track of
 * free blocks and best fit to find free block for malloc. The blocks
 * are in different lists according to their size. The lists are double
 * link lists. And when we need a free block, we use best fit to find a
 * free block whose size is closest to what we need. And when we free
 * a block, we find the corresponding list and insert it to the front of
 * the list.
 * Besides the last bit to represent alloc status, I took use of the 
 * second last bit of the header to record the alloc status of previous 
 * block. By doing this, the allocated block can only have header. This 
 * could increase the memory utilization. 
 * 
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
//#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define WSIZE 4
#define ALIGNMENT 8
#define MIN_BLOCK 16             /* The smallest block will be 16 */
#define SEGHEAD 36               /* Reserve space to place heads of lists */

#define LIST_NUM 8               /* Define how many lists we will use */
#define LIST_START 3             /* The first list */
#define STEP 2                   /* Use to adjust dividing groups */


#define CHUNKSIZE ((1<<8) + (1<<5))  /* Extend heap by this amount */

#define MAX(x, y)         ((x) > (y) ? (x) : (y))

/* Pack size and alloc status into one */
#define PACK(size, alloc) ((size) | (alloc))

/* Get a word from address and put a word into address */
#define GET(p)            (*(unsigned int *)(p))
#define PUT(p, val)       (*(unsigned int *)(p) = (val))

/* Get size, alloc status, previous block's allo status */
#define GET_SIZE(p)       (size_t)(GET(p) & ~(0x7))
#define GET_ALLOC(p)      (size_t)(GET(p) & 0x1)
#define GET_PALLOC(p)     (size_t)(GET(p) & 0x2)

/* Get header add and footer add */
#define HDRP(bp)          ((char *)(bp) - WSIZE)
#define FTRP(bp)          ((char *)(bp) + GET_SIZE(HDRP(bp)) - ALIGNMENT)
#define GET_SIZEBP(bp)    (size_t)(GET_SIZE(HDRP(bp)))

/* Get next add and pre add */
#define NEXT_BLKP(bp)     ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp)     ((char *)(bp) - GET_SIZE((char *)(bp) - ALIGNMENT))

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define SIZE_PTR(p)  ((size_t*)(((char*)(p)) - SIZE_T_SIZE))

/* Trans 64-bit addresses to 32-bit offset and trans back when using them*/
#define TRANS(rp)         (unsigned int)((long)((char *)(rp) - 0x800000000))
#define TRANS_BACK(op)    (char *)(((char *)(long)(op)) + 0x800000000)

/* Get the previous free block address and the next address */
#define PREV_FREE(bp)     (TRANS_BACK(GET(bp)))
#define NEXT_FREE(bp)     (TRANS_BACK(GET(bp + WSIZE)))

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p)          (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)



/* Helper functions */
static void *coalesce(void *bp);
static void insert_item(void *bp);
static void delete_item(void *bp);
static void *extend_heap(size_t size);
static void *place(void * bp, size_t size);
static void *find_fit(size_t size);
static int find_list(size_t size);
static int in_heap(const void *p);
static int aligned(const void *p);
static int check_block(char *b, int *c);


/* Global scalar variables */
static void *start_heap;          
static void *end_heap;            
static void *start_block;           
static void *heap_listp;


/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(3*WSIZE + SEGHEAD)) == (void *)-1) {
        return -1;
    }
    /* Reserve place for seg lists' headers */
    heap_listp = heap_listp + SEGHEAD;

    /* Initialize the global variables and lists' headers*/
    start_heap = mem_heap_lo();
    end_heap = mem_heap_hi() + 1;
    for(int i = 0; i < LIST_NUM; i++) {
        PUT(TRANS_BACK(i * WSIZE), 0);
    }
    
    PUT(heap_listp, PACK(ALIGNMENT, 1));          /* Prologue header */
    PUT(heap_listp + WSIZE, PACK(ALIGNMENT, 1));  /* Prologue footer */
    PUT((heap_listp + (WSIZE*2)), PACK(0, 3));    /* Epilogue header */

    /*Extend the empty heap with a free CHUNKSIZE block */
    if ((start_block = extend_heap(CHUNKSIZE)) == NULL) {
        return -1;
    }
    end_heap = mem_heap_hi() + 1;   /* end_heap changes */
    insert_item(start_block);       /* insert the free block into list*/
    return 0;
}


/*
 * malloc
 * 
 * Find a free block from free lists and allocate according to need.
 * If cannot find, extend the heap. Return the pointer of allocated
 * block.
 */
void *malloc (size_t size) {
    size_t asize;                /* Adjusted block size */
    size_t extendsize;           /* Extend size if cannot find fit */
    char *bp;
    char *bp_place;
    
    /* Ignore spurious requests */
    if (size == 0) {
        return NULL;
    }
    
    /* Adjust the size to meet requirement */
    if (size <= 3*WSIZE) {
        asize = 2*ALIGNMENT;
    }
    else {
        asize = ALIGN(size + WSIZE);
    }

    /* Search the lists to find a fit */
    if ((bp = find_fit(asize)) != NULL) {
        bp_place = place(bp, asize);
        return bp_place;
    }
    
    /* If not found, extend the heap */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize)) == NULL) {
        return NULL;
    }
    insert_item(bp);                     /* Insert this new large block */
    bp_place = place(bp, asize);         /* And place we need */
    return bp;
}

/*
 * free
 *
 * Given block pointer, free an allocated block
 */
void free (void *ptr) {
    if (ptr == NULL) {
        return;
    }
    size_t size = GET_SIZE(HDRP(ptr));
    size_t palloc = GET_PALLOC(HDRP(ptr));
    
    /* Set this block's header and footer */
    PUT(HDRP(ptr), PACK(size|palloc, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    
    /* Set the next block's header */
    size_t next = GET(HDRP(NEXT_BLKP(ptr))) & (~0x2);
    PUT(HDRP(NEXT_BLKP(ptr)), next);
    
    /* Coalesce and insert to list */
    ptr = coalesce(ptr);
    insert_item(ptr);
    return;
}

/*
 * realloc 
 *
 * Reacllocate block with new size.
 */
void *realloc(void *oldptr, size_t size) {
    size_t oldsize;
    void *newptr;

  /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        free(oldptr);
        return 0;
    }

  /* If oldptr is NULL, then this is just malloc. */
    if(oldptr == NULL) {
        return malloc(size);
    }

    newptr = malloc(size);

  /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

  /* Copy the old data. */
    oldsize = *SIZE_PTR(oldptr);
    if(size < oldsize) oldsize = size;
    memcpy(newptr, oldptr, oldsize);

  /* Free the old block. */
    free(oldptr);

    return newptr;
}

/*
 * calloc
 *
 * Allocate a block and set it to zero.
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
 */
void *calloc (size_t nmemb, size_t size) {
    size_t bytes = nmemb * size;
    void *newptr;

    newptr = malloc(bytes);
    memset(newptr, 0, bytes);

    return newptr;
}

/* coalesce
 *
 * This function is used to combine to adjacent free blocks and 
 * return the new block's pointer.
 *
 */
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_PALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    
    if (prev_alloc && next_alloc) {                   /* case 1 */
        return bp;
    }
    else if (prev_alloc && !next_alloc) {             /* case 2 */
        size += GET_SIZEBP(NEXT_BLKP(bp));
        /* The block whose size is less than min_block is not in lists*/
        if (GET_SIZEBP(NEXT_BLKP(bp)) >= MIN_BLOCK) {
            delete_item(NEXT_BLKP(bp));
        }
        PUT(HDRP(bp), PACK(size, 2));
        PUT(FTRP(bp), PACK(size, 0));
        return bp;
    }
    else if (!prev_alloc && next_alloc) {             /* case 3 */
        size += GET_SIZEBP(PREV_BLKP(bp));
        /* The blocks which size is less than min_block is not in lists*/
        if (GET_SIZEBP(PREV_BLKP(bp)) >= MIN_BLOCK) {
            delete_item(PREV_BLKP(bp));
        }
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 2));
        return PREV_BLKP(bp);
    }
    else {                                            /* case 4 */
        size += GET_SIZEBP(PREV_BLKP(bp)) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        /* The blocks which size is less than min_block is not in lists*/
        if (GET_SIZEBP(NEXT_BLKP(bp)) >= MIN_BLOCK) {
            delete_item(NEXT_BLKP(bp));
        }
        if (GET_SIZEBP(PREV_BLKP(bp)) >= MIN_BLOCK) {
            delete_item(PREV_BLKP(bp));
        }
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 2));
        return PREV_BLKP(bp);
    } 
}

/* 
 * extend_heap
 * 
 * This function is used to extend the heap when we need it.
 * 
 */
static void *extend_heap(size_t size) {
    char *bp;
	
	/* get the alloc status of the last, if not alloced, we can use it */
    size_t last_alloc = GET_PALLOC(end_heap - WSIZE);
    if (last_alloc == 0) {
        size = size - GET_SIZE(end_heap - ALIGNMENT);
    }
    
    if ((long)(bp = mem_sbrk(size)) == -1) {
        return NULL;
    }
    end_heap = mem_heap_hi() + 1;

    /* Initialize this block's header and place a new epilogue header */
    PUT(HDRP(bp), PACK(size|last_alloc, 0));
    PUT(FTRP(bp), PACK(size, 0));

    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return coalesce(bp);
}

/*
 * place
 * 
 * Given free block's pointer and needed size, this function place the
 * needed block into this and try to divide the block if there is place
 * left. Return the pointer of the allocated block.
 */
static void *place (void * bp, size_t size) {
    size_t free_size = GET_SIZE(HDRP(bp));
    char *next = NEXT_BLKP(bp);
    size_t remain = free_size - size;
    
    if (remain < MIN_BLOCK) {
        /* If the remaining place is less than MIN_BLOCK and more than 
           ALIGNMENT and at last of the heap, we divide it. For the last
           or not, it is just fast than dividing all the 8-byte block.*/
        if ((GET_SIZEBP(next) == 0) && (remain >= ALIGNMENT)) {
            PUT(HDRP(bp), PACK(size, 3));
            next = NEXT_BLKP(bp);
            PUT(HDRP(next), PACK(remain, 0x2));
            PUT(next, PACK(remain, 0));
            delete_item(bp);
            return bp;
        }
        /* If no remain or not in the end, we just use the whole space. */
        else {
            PUT(HDRP(bp), PACK(free_size, 3));
            delete_item(bp);
            next = NEXT_BLKP(bp);
            PUT(HDRP(next), GET(HDRP(next)) | (0x2));
            return bp;
        }
    }
    else {
        /* If the remain is larger than MIN_BLOCK, we divide it */
        PUT(HDRP(bp), PACK(size, 3));
        next = NEXT_BLKP(bp);
        PUT(HDRP(next), PACK(remain, 2));
        PUT(FTRP(next), PACK(remain, 0));
        delete_item(bp);
        insert_item(next);   /* Insert the new block to corresponding list*/
        return bp;
    }
}

/*
 * find_fit
 * 
 * We find the block with the closest size from the corresponding list
 * if, not found, then go to the next larger list. If found one, return 
 * the pointer. If not found in all lists, return NULL.
 */
static void *find_fit (size_t size) {
    int num = find_list(size);
    size_t best = (unsigned int)(~0x0);
    size_t f_size;
    char *tmp;
    char *free_block = start_heap;
    
    /* To search all lists */
    for (int i = num; i < LIST_NUM; i++) {
        /* Find the head of the list */
        tmp = TRANS_BACK(GET(TRANS_BACK(i*WSIZE))); 
        while (tmp != start_heap) {
            f_size = GET_SIZEBP(tmp);
            /* if find one with the same size, just return it*/
            if (f_size == size) {
                return tmp;
            }
            /* else find the closest */
            else if (size < f_size && f_size < best) {
                best = f_size;
                free_block = tmp;
            }
            tmp = NEXT_FREE(tmp);
        }
        /* If found one, just return, no need to search the next */
        if (free_block != start_heap) {
            return free_block;
        }
    }
    return NULL;
}

/*
 * find_list
 * 
 * Given the size, we find the corresponding list
 */
inline static int find_list(size_t size) {
    int i;
    for (i = 0; i < LIST_NUM; i++) {
        if (size <= (unsigned int)(1<<(i*STEP + LIST_START + 1))) {
            return i;
        }
    }
    return LIST_NUM - 1;
}

/*
 * delete_item
 *
 * Given the pointer, we delete the block from the free list
 */
inline static void delete_item (void *bp) {
    void *prev = TRANS_BACK(GET(bp));
    void *next = TRANS_BACK(GET(bp + WSIZE));
    /* if it is the first block in list, we store the next into the head*/
    if (prev < start_block) {
        PUT(prev, TRANS(next));
    }
    /* if in the list, we store the next into previous's next position*/
    else {
        PUT(prev + WSIZE, TRANS(next));
    }
    /* if the block is the last one, doing this will overwrite root */
    if (next != start_heap) {
        PUT(next, GET(bp));
    }
}

/*
 * insert_item
 *
 * Insert the item into the front of the corresponding list
 */
inline static void insert_item (void *bp) {
    int num = find_list(GET_SIZEBP(bp));
    char *start = TRANS_BACK(num*WSIZE);
    /* no block in list */
    if (TRANS_BACK(GET(start)) == start_heap) {
        PUT(start, TRANS(bp));
        PUT(bp, TRANS(start));
        PUT(bp + WSIZE, TRANS(start_heap));        
    }
    else {
        PUT(bp + WSIZE, GET(start));
        PUT(TRANS_BACK(GET(start)), TRANS(bp));
        PUT(start, TRANS(bp));
        PUT(bp, TRANS(start));
    }
    return;
}
/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void *p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
}


/*
 * mm_checkheap
 *
 * Check the heap to see if it is consistent, when it finds something
 * wrong, it will print an error message and exit.
 */
void mm_checkheap(int verbose) {
    verbose = verbose;
    void *tmp;
    int block_num = 0;
    void *pro;
    int free_one = 0;         /* count through blocks */
    int free_two = 0;         /* count through lists */

    /* Check the heap overall alignment */
    if ((aligned(start_heap) & aligned(end_heap)) == 0) {
        printf("Error with heap start and end address\n");
        exit(0);
    }
    
    /* Check the prologue block */
    pro = start_block - ALIGNMENT;
    if (GET(HDRP(pro)) != 0x09 || GET(FTRP(pro)) != 0x09) {
        printf("Error with the prologue block: %x --- %x at%p\n", 
                GET(HDRP(pro)), GET(FTRP(pro)), pro);
        exit(0);
    }
    
    /* Check heap boundary */
    if (in_heap(HDRP(end_heap)) == 0) {
        printf("Error with the heap boundary : %p\n", end_heap);
    }
    
    /* Check the epilogue block */
    tmp = HDRP(end_heap);
    if ((GET(tmp) & (~0x2)) != 0x1) {
        printf("Error with the epilogue block: %x\n", GET(tmp));
        exit(0);
    }
    
    /* Iterate to check all blocks */
    tmp = start_block;
    while (tmp < end_heap) {
        if (check_block(tmp, &free_one) == 0) {
            printf("Error with block %d at %p with size %d\n", 
                block_num, tmp, (unsigned int)GET_SIZEBP(tmp));
            exit(0);
        }
        tmp = NEXT_BLKP(tmp);
        block_num++;
    }
    
    /* Iterate to check segregated lists */
    for (int i = 0; i < LIST_NUM; i++) {
        /* Find the head of the list */
        tmp = TRANS_BACK(GET(TRANS_BACK(i*WSIZE))); 
        while (tmp != start_heap) {
            /* Check if all pointers inboud */
            if (!in_heap(TRANS_BACK(GET(tmp)))) {
                printf("Previous pointer at: %p\n out of bound", tmp);
                exit(0);
            }
            if (!in_heap(TRANS_BACK(GET(tmp + WSIZE)))) {
                printf("Next pointer at: %p\n out of bound", tmp);
                exit(0);
            }
            
            /* Check inlist blocks' pointers consistency */
            if (GET(tmp) > SEGHEAD) {
                if (GET(TRANS_BACK(GET(tmp)) + WSIZE) != TRANS(tmp)) {
                    printf("Error with previous pointer at: %p\n", tmp);
                    exit(0);
                }
            }
            if (GET(tmp + WSIZE) != 0) {
                if (GET(TRANS_BACK(GET(tmp + WSIZE))) != TRANS(tmp)) {
                    printf("Error with next pointer at: %p\n", tmp);
                    exit(0);
                }
            }
            /* Check if the size is right to be in this list */
            if (i < LIST_NUM -1) {
                if (GET_SIZEBP(tmp) > (unsigned)(2<<(i*STEP + LIST_START))) {
                    printf("Error with size at: %p\n", tmp);
                    exit(0);
                }
            }
            free_two = free_two + 1;
            tmp = NEXT_FREE(tmp);
        }
    }
    
    /* If the two numbers do not agree, means something wrong */
    if (free_one != free_two) {
        printf("Error with free block numbers\n");
        exit(0);
    }
    
    dbg_printf("ALL GOOD\n");
    
}

/*
 * check_block
 * 
 * helper function for mm_checkheap, check one block and return 0 if
 * there is something wrong, 1 if it is good.
 */
static int check_block(char *b, int *c) {
    /* Check if it is aligned */
    if (aligned(b) == 0) {
        printf("Not aligned\n");
        return 0;
    }
    
    /* For free block, check if the header and footer are identical*/
    if (GET_ALLOC(HDRP(b)) == 0) {
        if (GET_SIZEBP(b) > ALIGNMENT) {
            *c = *c + 1;
        }
        if ((GET(HDRP(b)) & (~0x2)) != (GET(FTRP(b)))) {
            printf("Not identical: %x --- %x\n", GET(HDRP(b)), GET(FTRP(b)));
            return 0;
        }
    }
    
    /* Check for adjacent free blocks */
    if (GET_ALLOC(HDRP(b)) == 0) {
        if ((!GET_PALLOC(HDRP(b))) | (!GET_ALLOC(HDRP(NEXT_BLKP(b))))) {
            printf("Two consecutive free blocks at: %p\n", b);
            return 0;
        }
    }
    
    /* Check alloc status correctness */
    if (GET_PALLOC(HDRP(b)) == 0) {
        if (GET_ALLOC(HDRP(PREV_BLKP(b))) != 0) {
            printf("Alloc status not correct at: %p\n", b);
            return 0;
        }
    }
    
    /* Check if the foot is out of heap */
    if (in_heap((char *)FTRP(b)) == 0) {
        printf("Out of bound\n");
        return 0;
    }
    
    return 1;
}
