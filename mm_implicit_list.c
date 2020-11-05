// TODO: Give credit to Book for code snippets
// TODO: Write comment of my solution up here
/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

team_t team = {
    /* Team name */
    "",
    /* First member's full name */
    "",
    /* First member's email address */
    "",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

#define DEBUG 1
#define HEAPCHECK 1
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE 4096

#define debugprint(format, args...) if (DEBUG) printf(format, ## args)

#define MAX(x, y) (x > y ? x : y)

#define PACK(size, alloc) (size | alloc)

#define GET(p) (*(size_t *)p)
#define PUT(p, val) (*(size_t *)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp) ((char *)bp - WSIZE)
#define FTRP(bp) ((char *)bp + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)bp + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)bp - GET_SIZE(((char *)bp - DSIZE)))

#define GET_PAYLOAD_SIZE(bp) (GET_SIZE(HDRP(bp)) - DSIZE)




void *heap_listp;

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {             /* Case 1 */
        return bp;
    } else if (prev_alloc && !next_alloc) {     /* Case 2 */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));

        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
    } else if (!prev_alloc && next_alloc) {     /* Case 3 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));

        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));

        bp = PREV_BLKP(bp);
    } else {                                    /* Case 4 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) +
            GET_SIZE(FTRP(NEXT_BLKP(bp)));

        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));

        bp = PREV_BLKP(bp);
    }
    return bp;
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0)); /* Free block header */
    PUT(FTRP(bp), PACK(size, 0)); /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0); /* Alignment padding */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); /* Epilogue header */
    heap_listp += (2*WSIZE);

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

static void *find_fit(size_t asize) {
    // TODO: Make this more concise (no need for this much debug)
    debugprint("\n******** FINDING FIT FOR %i BYTES *********\n", asize);

    void *bp = (char *)heap_listp;

    while (1) {
        debugprint("Checking %i/%i (%p)\n", GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)), bp);

        if (GET_SIZE(HDRP(bp)) == 0) {
            debugprint("************ No match found ************\n");
            return NULL;
        }

        if (!GET_ALLOC(HDRP(bp)) && GET_PAYLOAD_SIZE(bp) >= asize) {
            debugprint("******* Found match *********\n");
            break;
        }

        bp = NEXT_BLKP(bp);
    }

    return bp;
}

static void place(void *bp, size_t asize) {
    //printf("\nPlacing %i at %p (%i/%i)", asize, bp, GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)));

    // Add DSIZE because of header and footer both take up 4 bytes (WSIZE)
    int newSize = asize + DSIZE;
    int splitSize = GET_SIZE(HDRP(bp)) - newSize;
    int newBoundaryTag = PACK(newSize, 1);

    char *newFtr = (char *)bp + newSize - DSIZE;
    PUT(HDRP(bp), newBoundaryTag);
    PUT(newFtr, newBoundaryTag);

    if (splitSize > 0) {
        // Split
        int freeBoundaryTag = PACK(splitSize, 0);
        char *splitHdr = (char *)bp + newSize - WSIZE;
        char *splitFtr = splitHdr + splitSize - WSIZE;

        PUT(splitHdr, freeBoundaryTag);
        PUT(splitFtr, freeBoundaryTag);
    }
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size) {
    mm_check();
    size_t asize; /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        debugprint("\nFound fit for %i (adjusted to %i) at %i/%i (%p)\n", size, asize, GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)), bp);
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL) {
        printf("No more memory... ERR\n");
        return NULL;
    }
    debugprint("\nNo fit found, but heap was extended by %i. Following was placed: %i (adjusted to %i) at %i/%i (%p)\n", extendsize, size, asize, GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)), bp);
    place(bp, asize);
    return bp;
}


/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr) {
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
    size_t asize;
    if (size <= DSIZE)
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);


    if (asize == 0) {
        mm_free(ptr);
        return NULL;
    }
    if (ptr == NULL) return mm_malloc(asize);

    if (GET_PAYLOAD_SIZE(ptr) > asize) {
        // Can just expand out
        //
        //printf("\nprev size: %i, new size (%i - %i) [%p]", GET_PAYLOAD_SIZE(ptr), size, asize, ptr);
        //PUT(HDRP(ptr), PACK(asize, 1));
        //PUT(FTRP(ptr), PACK(asize, 1));
        //printf("\nprev size: %i, new size (%i - %i) [%p]", GET_PAYLOAD_SIZE(ptr), size, asize, ptr);
        //return ptr;
    } else {
        void *nextBp = NEXT_BLKP(ptr);
        if (!GET_ALLOC(HDRP(nextBp)) && GET_PAYLOAD_SIZE(ptr) + GET_PAYLOAD_SIZE(nextBp) - DSIZE >= asize) {
            // Can expand out into next block
        }
    }
    // Must find brand new block
    size_t copySize = GET_SIZE(ptr) - WSIZE;
    void *newPtr = mm_malloc(asize);
    if (newPtr == NULL) return NULL;
    if (asize < copySize) copySize = asize;

    memcpy(newPtr, ptr, copySize);
    mm_free(ptr);

    return newPtr;
}

int mm_check() {
    if (!HEAPCHECK) return 1;
    debugprint("\n\n---------- HEAP CHECK ----------\n\n");

    char *bp = heap_listp;
    while (GET_SIZE(HDRP(bp)) != 0) {
        debugprint("| %i/%i | ... | %i/%i ", GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)), GET_SIZE(FTRP(bp)), GET_ALLOC(FTRP(bp)));
        bp = NEXT_BLKP(bp);
    }
    debugprint("| %i/%i |", GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)));

    debugprint("\n\n---------- HEAP CHECK ----------\n\n");
    return 1;
}

