/*
 * Explicit free list implementation of a malloc package.
 * We keep a list of only the free blocks, to make the allocation only require
 * a linear search in the list of free blocks, and not all blocks (herein
 * including allocated), which in most cases is a much larger list. The list
 * insertion policy implemented here is LIFO, where we always logically insert
 * new free blocks at the root of the list, and don't order them by their
 * addresses. 
 * 
 * Credit: Macros and certain implementation functions inspired from the book:
 * Computer Systems - A Programmer's Perspective by Bryant & O'Hallaron *
 * (978-0-13-610804-7)
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


/*
 * Function declarations
 */
int mm_check();

#define DEBUG 0
#define HEAP_CHECK 0
#define PRINT_LISTS 0
#define WSIZE 4
#define DSIZE (2 * WSIZE) // Must be double word
#define CHUNKSIZE 4096

#define debugprint(format, args...) if (DEBUG) printf(format, ## args)

/*
 * Credit: Most of these macros taken from the course text-book (information and ISBN in top comment)
 */
#define MAX(x, y) (x > y ? x : y)

#define PACK(size, alloc) (size | alloc)

#define GET(p) (*(size_t *)p)
#define GET_ADDR(p) (*(void **)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))
#define PUT_ADDR(p, addr) (*(void **)(p) = (addr));    

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp) ((char *)bp - WSIZE)
#define FTRP(bp) ((char *)bp + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXTP(bp) (bp)
#define PREVP(bp) ((char *)bp + WSIZE)

#define NEXT_BLKP(bp) ((char *)bp + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)bp - GET_SIZE(((char *)bp - DSIZE)))

#define ALIGN(size) (size <= DSIZE ? 2*DSIZE : DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE))

#define IS_IN_RANGE(bp) ((((size_t) mem_heap_lo()) <= ((size_t) bp)) && (((size_t) mem_heap_hi()) >= ((size_t) bp)))

// Root of the free list (implementation)
void *root;

// This is simply used to point to the physical start of the heap. Only used for
// debugging and visually printing of lists, and is in no way related to the
// implementation of the explicit free list.
void *heap_listp;

/*
 * This helper function logically removes a block from the free block list.
 * This is done by essentially and logically "skipping" the block pointer in
 * question.
 */
static void removeBlock(void *bp) {
    // Assumption: bp is a block in the free list. I.e. it must have
    // header/footer, and either a valid address or NULL in the next/prev
    // pointer. If this is not the case, a segfault will probably happen.

    // Logically skip bp
    void *logicalNext = GET_ADDR(NEXTP(bp));
    void *logicalPrev = GET_ADDR(PREVP(bp));
    if (logicalNext) PUT_ADDR(PREVP(logicalNext), logicalPrev);
    if (logicalPrev) {
        PUT_ADDR(NEXTP(logicalPrev), logicalNext);
    } else {
        if (logicalNext) {
            // No previous but one next, then this next should be the new root
            root = logicalNext;
        } else {
            // No previous and next, then we are at a list with 1 block, and we want
            // to remove that, so set root to NULL.
            root = NULL;
        }
    }
}

/*
 * This helper function is used to just update both the header and footers of a
 * given block to some boundary tag.
 */
static void updateBlockTags(void *bp, size_t boundaryTag) {
    PUT(HDRP(bp), boundaryTag);
    PUT(FTRP(bp), boundaryTag);
}

/*
 * Helper function to logically insert a new free block into the free list.
 */
static void insertNewBlock(void *bp) {
    // If empty list (no root), then we simply set root to the block
    if (!root) {
        // Make sure that the block you insert has NULL next/prev pointers
        PUT_ADDR(NEXTP(bp), NULL);
        PUT_ADDR(PREVP(bp), NULL);
        root = bp;
        return;
    }

    // Change root
    void *oldRoot = root;
    root = bp;

    // Make the old root have a previous pointer to our newly inserted root
    PUT_ADDR(PREVP(oldRoot), bp);

    // Update our new root's next/prev pointers
    PUT_ADDR(NEXTP(bp), oldRoot);
    PUT_ADDR(PREVP(bp), NULL);
}

/*
 * Physically coalesce a block by checking multiple cases:
 * Case 1: Next free, previous allocated
 * Case 2: Next allocated, previous free
 * Case 3: Next free, previous free
 *
 * Coalescing in the terms of reducing fragmentation by joining together free
 * blocks when possible.
 */
static void *coalesce(void *bp) {
    // Phsyical next and prev blocks
    void *next = NEXT_BLKP(bp);
    void *prev = PREV_BLKP(bp);
    size_t nextAlloc = GET_ALLOC(HDRP(next));
    size_t prevAlloc = GET_ALLOC(HDRP(prev));

    // We have two edge cases because of the way we extend the heap. If we are
    // coalescing at the starting edge of the physical list, and the succeeding
    // block is free, we can still coalesce these two free blocks in as a case 1
    // condition. The same with case 2 in terms of the end of the physical list.
    int case1Edge = !nextAlloc && !IS_IN_RANGE(prev);
    int case2Edge = !prevAlloc && !IS_IN_RANGE(next);

    // Case 1 (next free, previous allocated):
    if ((!nextAlloc && prevAlloc && IS_IN_RANGE(next) && IS_IN_RANGE(prev)) || case1Edge) {
        // Remove the old free blocks from the list (logically)
        removeBlock(bp);
        removeBlock(next);

        // Make this block into the bigger coalesced block (physically)
        // The size is gonna be the size of this + the size of the next block
        size_t newBoundaryTag = PACK((GET_SIZE(HDRP(next)) + GET_SIZE(HDRP(bp))), 0);
        updateBlockTags(bp, newBoundaryTag);

        // Add the new block to the free list (with LIFO ordering) (logically)
        insertNewBlock(bp);
        return bp;
    }

    // Case 2 (next allocated, previous free):
    if ((nextAlloc && !prevAlloc && IS_IN_RANGE(next) && IS_IN_RANGE(prev)) || case2Edge) {
        // Remove the old free blocks from the list (logically)
        removeBlock(prev);
        removeBlock(bp);

        // Make the previous block into the bigger coalesced block (physically)
        // The size is gonna be the size of this + the size of the previous block
        size_t newBoundaryTag = PACK((GET_SIZE(HDRP(prev)) + GET_SIZE(HDRP(bp))), 0);
        updateBlockTags(prev, newBoundaryTag);

        // Add the new block (address of expanded previous block) to the free
        // list (with LIFO ordering) (logically)
        insertNewBlock(prev);
        return prev;
    }

    // Case 3 (both free)
    if (!nextAlloc && !prevAlloc && IS_IN_RANGE(next) && IS_IN_RANGE(prev)) {
        // Remove the old free blocks from the list (logically)
        removeBlock(prev);
        removeBlock(bp);
        removeBlock(next);

        // Make the previous block into the bigger coalesced block (physically)
        // The size is gonna be the size of this + the size of the previous block + the size of the next block
        size_t newBoundaryTag = PACK((GET_SIZE(HDRP(prev)) + GET_SIZE(HDRP(bp) + GET_SIZE(HDRP(next)))), 0);
        updateBlockTags(prev, newBoundaryTag);

        // Add the new block (address of expanded previous block) to the free
        // list (with LIFO ordering) (logically)
        insertNewBlock(prev);
        return prev;
    }

    return bp;
}

/*
 * Helper function to extend the available heap memory by a given amount. Measured in
 * words, so words/wordsize bytes.
 */
static void *extend_heap(size_t words) {
    debugprint(" \n ********* EXTENDING HEAP WITH %i WORDS ********* \n ", words);
    void *oldRoot = root;
    size_t size;

    // Get words in bytes and have it properly aligned
    /* Credit: Course textbook */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((root = mem_sbrk(size)) == (void *)-1)
        return NULL;

    /* Make new root and set its next to prev root */
    // Alignment padding
    root += 4;
    PUT(root, PACK(size, 0)); // Header
    root += WSIZE; // go from header to block pointer
    PUT_ADDR(NEXTP(root), oldRoot); // Old root next pointer
    PUT_ADDR(PREVP(root), NULL); // NULL prev pointer
    PUT(FTRP(root), PACK(size, 0)); // Footer

    // Also set the oldRoot's previous pointer to point to this new root.
    // NULL guard required in case of finding a perfect fit when only 1 free
    // block is present (oldRoot is in that case NULL.)
    if (oldRoot) PUT_ADDR(PREVP(oldRoot), root);

    // Coalesce and return the newly created root
    return coalesce(root);
}

/* 
 * The initialization function to setup the malloc package to be ready to use.
 * This is required to be called before usage, as we must uphold a proper heap-
 * and free list structure and block alignment.
 */
int mm_init(void) {
    // Allocate memory to initialize the empty heap.
    /* Credit: Course textbook */
    if ((root = mem_sbrk(CHUNKSIZE)) == (void *)-1)
        return -1;

    // Alignment padding
    root += 4;
    PUT(root, PACK(CHUNKSIZE, 0)); // Header
    root += WSIZE; // go from header to block pointer
    PUT_ADDR(NEXTP(root), NULL); // NULL next pointer
    PUT_ADDR(PREVP(root), NULL); // NULL prev pointer
    PUT(FTRP(root), PACK(CHUNKSIZE, 0)); // Footer

    // Only used for debugging (printing of lists) 
    heap_listp = root;

    return 0;
}

/*
 * Helper function used to find the first fit for a given size on the heap.
 * Returns: a pointer to a block which is able to fit "asize" bytes as payload.
 */
static void *find_fit(size_t asize) {
    // This function could be made way more concise, but its left verbose to
    // have detailed debugging information.
    debugprint("\n******** FINDING FIT FOR %i BYTES *********\n", asize);

    void *bp = root;

    while (1) {
        debugprint("Checking %i/%i (%p) [%p / %p]\n", GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)), bp, *(void **)bp, *(void **)(bp + WSIZE));
        // Check if the payload fits in this block.
        // "asize" is already adjusted to include overhead (i.e. only payload size)
        if (GET_SIZE(HDRP(bp)) >= asize) {
            debugprint("******* Found match *********\n");
            break;
        }

        // If no next pointer and we couldnt fit payload in this block, we did not find a match
        if (GET_ADDR(NEXTP(bp)) == NULL) {
            debugprint("************ No match found ************\n");
            return NULL;
        }

        bp = GET_ADDR(NEXTP(bp));
    }

    return bp;
}

/*
 * Helper function to place (allocate) a block pointer to a given size. The
 * function will also split the free block into an allocated block and another
 * (smaller) free block, if the size allows it.
 * 
 * Assumption: block pointer given must be free, and asize must fit in the size.
 */
static void place(void *bp, size_t asize) {
    // Split size is the current size of the free block minus the new size that we must fit into this free block
    int splitSize = GET_SIZE(HDRP(bp)) - asize;
    // This is the boundary tag for the allocated block of the given size
    // As we don't split on 8 or less, we must add to the size of the size
    // in the allocated block, else it doesn't point correctly over the
    // internal fragmentation.
    if (splitSize == DSIZE) asize = asize + DSIZE;

    // Set the new block tags
    size_t newBoundaryTag = PACK(asize, 1);
    updateBlockTags(bp, newBoundaryTag);

    // Previous/Next pointers from this block pointer (old free block)
    void *prevp = GET_ADDR(PREVP(bp));
    void *nextp = GET_ADDR(NEXTP(bp));

    // If the placed block was smaller than the free block, splitSize will be
    // greater than 0. If the split size is greater than the size of a double
    // word, we will split (this is to reduce external fragmentation by having a
    // lot of unusably tiny free blocks)
    if (splitSize > DSIZE) {
        // Boundary tag of new free block
        int freeBoundaryTag = PACK(splitSize, 0);
        // New free block is going to be placed as the physical next (NEXT_BLKP) from the previous allocated block.
        void *newNext = NEXT_BLKP(bp);
        // Update its boundary tags
        updateBlockTags(newNext, freeBoundaryTag);

        // If previous pointer is NULL we are at the first free block (directly proceeding root)
        if (!prevp) root = newNext;
        // If not at root we must update the previous pointer's next pointer to the proper new address
        else PUT_ADDR(NEXTP(prevp), newNext);

        // If next pointer is null, we don't need to update next pointer's previous pointer.
        // But if there is one more free block, we must ensure that its previous pointer gets updated
        if (nextp) PUT_ADDR(PREVP(nextp), newNext);

        // Obviously also set this free block's next and previous pointer
        PUT_ADDR(NEXTP(newNext), nextp);
        PUT_ADDR(PREVP(newNext), prevp);

        debugprint("\n***** After place (and split): *****");
        debugprint("\n Placed block (%p): | %i/%i | ... | %i/%i |", bp, GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)), GET_SIZE(FTRP(bp)), GET_ALLOC(FTRP(bp)));
        debugprint("\n Free block (%p): | %i/%i | ( %p ) ( %p ) ... | %i/%i |", newNext, GET_SIZE(HDRP(newNext)), GET_ALLOC(HDRP(newNext)), GET_ADDR(NEXTP(newNext)), GET_ADDR(PREVP(newNext)), GET_SIZE(FTRP(newNext)), GET_ALLOC(FTRP(newNext)));
        debugprint("\n***** After place (and split): ***** \n\n");
    } else {
        debugprint("\n\n ***** Found perfect fit, removing free block from list. Splitsize: %i ***** \n\n", splitSize);
        // In this case we found the perfect fit for a payload and a free block, thus just remove the free block from the free list
        removeBlock(bp);
        // If the first allocated block is a perfect fit, removing it would
        // result in no free blocks (a NULL root), we obviously don't want that.
        // So we must extend the heap after placing it in this case.
        if (!root) {
            extend_heap(CHUNKSIZE/WSIZE);
        }
    }
    mm_check();
}

/* 
 * One of the core functions of the mm package. mm_malloc is used to explicitly
 * allocate a given heap space to be used in your program.
 */
void *mm_malloc(size_t size) {
    size_t extendsize;
    size_t asize;
    char *bp;

    // Ignore bad requests
    if (size == 0) return NULL;

    // Adjust the size of the block to be aligned and include the overhead of boundary tags.
    asize = ALIGN(size);

    // Find a fit by searching the free block list for a fit
    if ((bp = find_fit(asize))) {
        debugprint("\nFound fit for %i (adjusted to %i) at %i/%i (%p)\n", size, asize, GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)), bp);
        place(bp, asize);
        mm_check();
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize,CHUNKSIZE);
    if (!(bp = extend_heap(extendsize/WSIZE))) {
        printf("ERROR: No more memory!\n");
        return NULL;
    }
    debugprint("\nNo fit found, but heap was extended by %i. Following is going to be placed: %i (adjusted to %i) at %i/%i (%p)\n", extendsize, size, asize, GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)), bp);
    place(bp, asize);
    mm_check();
    return bp;
}


/*
 * Another one of the core functions of the mm package. mm_free is used to
 * explicitly free a block of heap memory to allow it to be re-used in the
 * future. This is done by examining 4 cases:
 * 
 * Case 1: Next and previous blocks are both allocated.
 * Case 2: Next is allocated, previous is free.
 * Case 3: Next is free, previous is allocated.
 * Case 4: Both blocks are free
 */
void mm_free(void *ptr) {
    // Physical next and prev blocks
    void *next = NEXT_BLKP(ptr);
    void *prev = PREV_BLKP(ptr);
    // The allocated bit of the next/prev blocks
    size_t nextAlloc = GET_ALLOC(HDRP(next));
    size_t prevAlloc = GET_ALLOC(HDRP(prev));

    // Case 1
    if (nextAlloc && prevAlloc && IS_IN_RANGE(next) && IS_IN_RANGE(prev)) {
        debugprint(" \n *** Case 1 freeing of: %p (%i/%i) *** \n ", ptr, GET_SIZE(HDRP(ptr)), GET_ALLOC(HDRP(ptr)));
        // Update the physical block tags to be unallocated
        updateBlockTags(ptr, PACK(GET_SIZE(HDRP(ptr)), 0));

        // Insert the new free block at the root of the list
        insertNewBlock(ptr);
        mm_check();
        return;
    }


    // Case 2
    if (nextAlloc && !prevAlloc && IS_IN_RANGE(next) && IS_IN_RANGE(prev)) {
        debugprint(" \n *** Case 2 freeing of: %p (%i/%i) *** \n ", ptr, GET_SIZE(HDRP(ptr)), GET_ALLOC(HDRP(ptr)));
        // Not "re-using" coalesce function here as it unnecesarily removes the "to-be" freed block
        // Remove the predecessor block
        removeBlock(prev);

        // Extend prev block
        updateBlockTags(prev, PACK((GET_SIZE(HDRP(prev)) + GET_SIZE(HDRP(ptr))), 0));

        // Insert the block (previous) at the root of the free list
        insertNewBlock(prev);
        mm_check();
        return;
    }

    // Case 3
    if (!nextAlloc && prevAlloc && IS_IN_RANGE(next) && IS_IN_RANGE(prev)) {
        debugprint(" \n *** Case 3 freeing of: %p (%i/%i) *** \n ", ptr, GET_SIZE(HDRP(ptr)), GET_ALLOC(HDRP(ptr)));
        // Not "re-using" coalesce function here as it unnecesarily removes the "to-be" freed block
        // Remove the successor block
        removeBlock(next);

        // Extend this current "to-be" freed block
        updateBlockTags(ptr, PACK((GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(next))), 0));

        // Insert this block at the root of the free list
        insertNewBlock(ptr);
        mm_check();
        return;
    }

    // Case 4
    if (!nextAlloc && !prevAlloc && IS_IN_RANGE(next) && IS_IN_RANGE(prev)) {
        debugprint(" \n *** Case 4 freeing of: %p (%i/%i) *** \n ", ptr, GET_SIZE(HDRP(ptr)), GET_ALLOC(HDRP(ptr)));
        // Remove the predecessor and successor blocks
        removeBlock(prev);
        removeBlock(next);

        // Extend prev block
        updateBlockTags(prev, PACK((GET_SIZE(HDRP(prev)) + GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(next))), 0));

        // Insert this block at the root of the free list
        insertNewBlock(prev);
        mm_check();
        return;
    }
}

/*
 * The last of the core functions of the mm package. mm_realloc is used to
 * re-allocate a portion of memory, effectively attempting to re-size that
 * block of heap.
 * 
 * The implementation of this realloc is not very well polished, and only has a
 * simple case check where the next block is free and we can thus effectively
 * extend the current block out into the next free block, and update the
 * logical and physical blocks. The implementation could utilize the space
 * better if it also checked the cases of the previous block being free, and
 * both adjacent blocks being free.
 * 
 * Other than that, we also have some special cases of the use of mm_realloc,
 * which is simply that if supplied with a NULL ptr, it is equivalent to
 * calling mm_malloc with the supplied size. Likewise, if supplied with a size
 * of 0, it is equivalent to an mm_free call, with the given ptr.
 */
void *mm_realloc(void *ptr, size_t size) {
    size_t oldSize;
    void *newAllocBlock;
    size_t asize;
    asize = ALIGN(size);

    debugprint(" \n *** REALLOCATING %p (%i/%i) [payload size: %i] to %i (adjusted to %i) *** \n ", ptr, GET_SIZE(HDRP(ptr)), GET_ALLOC(HDRP(ptr)), (GET_SIZE(HDRP(ptr)) - DSIZE), size, asize);

    // Ignore spurious requests
    // If the size of the current block is equal to what we want to realloc it to, ignore it.
    if (GET_SIZE(HDRP(ptr)) == asize) return ptr;

    // Simple base cases by definition of realloc
    if (!ptr) return mm_malloc(size);
    if (size == 0) {
        mm_free(ptr);
        return 0;
    }

    // Case 1 (next block is free, and there is enough room to just extend current allocated block out into the free block)
    void *next = NEXT_BLKP(ptr);
    size_t extendedBlockPayloadSize = GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(next));
    if (next && !GET_ALLOC(HDRP(next)) && IS_IN_RANGE(next) && asize < extendedBlockPayloadSize) {
        // Logically remove the old free block
        removeBlock(next);
        int splitSize = extendedBlockPayloadSize - asize;
        // As we don't split on 8 or less, we must add to the size of the size
        // in the allocated block, else it doesn't point correctly over the
        // internal fragmentation. (this is identical to the place function)
        if (splitSize == DSIZE) asize = asize + DSIZE;
        // Extend this allocated block out
        updateBlockTags(ptr, PACK(asize, 1));

        // If split size is greater than 8, then we should split a new free block in afterwards
        // Else the size of this current allocated block will not be pointing to the proper next block anymore
        if (splitSize > 8) {
            // This will point to where the new free block should be placed
            void *freeBlock = NEXT_BLKP(ptr);
            // Now give this free block proper tags and size
            updateBlockTags(freeBlock, PACK(splitSize, 0));
            // Lastly, add it to the free block list
            insertNewBlock(freeBlock);
        } else {
            // We found a perfect fit.
            // There is still an edge case here if the free block we expanded
            // into was the only free block, then by removing it, we might have
            // set it to NULL. In that case, obviously expand the heap.
            if (!root) {
                extend_heap(CHUNKSIZE/WSIZE);
            }
        }

        mm_check();
        return ptr;
    }

    newAllocBlock = mm_malloc(size);

    // Simply propagate an error from malloc through this function in case of an error.
    if (!newAllocBlock) return 0;

    // Copy the payload data from the old allocated block to our new block
    oldSize = GET_SIZE(HDRP(ptr)) - DSIZE;
    // Obviously there is no need to copy more data than the new block will contain.
    if (size < oldSize) oldSize = size;
    // Copy the memory using memcpy
    memcpy(newAllocBlock, ptr, oldSize);

    // Free the old free block
    mm_free(ptr);

    return newAllocBlock;
}

/*
 * ONLY FOR DEBUGGING PURPOSES.
 * Small helper function that just fills a given char buffer with "amount" of
 * "c" by repeatedly concatenating it on the buffer.
*/
static void fillBufferWithChars(char *buf, int amount, char *c) {
    for (int i = 0; i < amount; i++) strcat(buf, c);
}

/*
 * ONLY FOR DEBUGGING PURPOSES.
 * Function used to visualize the free list and the heap at runtime - useful for debugging.
 *
 * It will print out the lists in the following format:
 * 
 *             FREE LIST
 * +------------+----+------------+
 * | 2024/0     |    | 2104/0     |
 * | 0xf6961830 |    | 0xf695f018 |
 * +------------+----+------------+
 * | 0xf695f018 | -> | (nil)      |
 * | (nil)      | <- | 0xf6961830 |
 * +------------+----+------------+
 * 
 *                      HEAP LIST
 * +--------+----+--------+----+--------+----+--------+
 * | 2104/0 | -> | 4080/1 | -> | 4080/1 | -> | 2024/0 |
 * +--------+----+--------+----+--------+----+--------+
 * 
*/
static void printLists() {
    void *bp = root;
    char freeListBuffer[8000] = "";
    char freeListAddrBuffer[8000] = "";
    char freeListNextBuffer[8000] = "";
    char freeListPrevBuffer[8000] = "";

    char size[200];
    char addr[200];
    char addedSpaces[100];
    char padding[50] = "";
    int i = 0;

    // I simply guard the while condition with a maximum of 10 iterations.
    while (bp && i < 10) {
        i++;
        void *nextp = GET_ADDR(NEXTP(bp));
        void *prevp = GET_ADDR(PREVP(bp));

        sprintf(size, "%s| %i/%i ", padding, GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)));
        sprintf(addr, "%s| %p |", padding, bp);

        addedSpaces[0] = '\0';
        fillBufferWithChars(addedSpaces, (strlen(addr) - strlen(size) - 1), " ");

        sprintf(freeListBuffer + strlen(freeListBuffer), "%s%s|", size, addedSpaces);
        strcat(freeListAddrBuffer, addr);
        sprintf(freeListNextBuffer + strlen(freeListNextBuffer), "%s| %p %s|", (prevp != NULL ? " -> " : ""), nextp, (nextp == NULL ? "     " : ""));
        sprintf(freeListPrevBuffer + strlen(freeListPrevBuffer), "%s| %p %s|", (prevp != NULL ? " <- " : ""), prevp, (prevp == NULL ? "     " : ""));

        // By padding like this, we avoid strcpy'ing on each iteration
        if (!prevp) strcpy(padding, "    ");

        bp = GET_ADDR(NEXTP(bp));
    }

    char dashes[200] = "";
    for (int i = 0; i < strlen(freeListBuffer); i++)
        freeListBuffer[i] == '|' ? strcat(dashes, "+") : strcat(dashes, "-");

    char titlePadding[100] = "";
    fillBufferWithChars(titlePadding, ((strlen(dashes) / 2) - 5), " ");
    debugprint("\n\n%sFREE LIST\n%s\n%s\n%s\n%s\n%s\n%s\n%s", titlePadding, dashes, freeListBuffer, freeListAddrBuffer, dashes, freeListNextBuffer, freeListPrevBuffer, dashes);



    // "Reset" all arrays
    freeListBuffer[0] = '\0';
    padding[0] = '\0';
    titlePadding[0] = '\0';
    dashes[0] = '\0';
    i = 0;

    bp = heap_listp;
    // I simply guard the while condition with a maximum of 10 iterations.
    while (IS_IN_RANGE(bp) && i < 10) {
        i++;
        sprintf(size, "%s| %i/%i |", padding, GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)));

        strcat(freeListBuffer, size);

        // By padding like this, we avoid strcpy'ing on each iteration
        if (bp == heap_listp) strcpy(padding, " -> ");

        bp = NEXT_BLKP(bp);
    }

    for (int i = 0; i < strlen(freeListBuffer); i++)
        freeListBuffer[i] == '|' ? strcat(dashes, "+") : strcat(dashes, "-");

    fillBufferWithChars(titlePadding, ((strlen(dashes) / 2) - 5), " ");
    debugprint("\n\n%sHEAP LIST\n%s\n%s\n%s", titlePadding, dashes, freeListBuffer, dashes);
}

/*
 * Heap consistency checker
 */
int mm_check() {
    if (!HEAP_CHECK) return 1;
    // Visually print both the free list and the heap list
    if (PRINT_LISTS) printLists();

    // Small helper macro for printing an error and returning 0.
    #define PRINT_AND_FAIL(str) { printf(" \n ****** HEAP INCONSISTENCY FOUND: \"%s\" ******** \n ", str); return 0; }

    // Check the free list for inconsistencies. Most of the code here should be
    // self explanatory, and the strings being printed will tell what is being
    // checked.
    void *bp = root;
    while (bp) {
        void *hdr;
        void *next;
        void *prev;
        void *physNext;
        void *physPrev;

        hdr = HDRP(bp);
        // "Are there any free blocks with a size of zero?"
        if (GET_SIZE(hdr) == 0) PRINT_AND_FAIL("A free block has a size of zero.");
        // "Is every block in the free list marked as free?"
        if (GET_ALLOC(hdr)) PRINT_AND_FAIL("A \"free\" block has the allocated bit set.");

        // "Do the pointers in the free list point to valid free blocks?"
        next = GET_ADDR(NEXTP(bp));
        prev = GET_ADDR(PREVP(bp));
        if (next && GET_ALLOC(HDRP(next))) PRINT_AND_FAIL("A free block is pointing (next) to a non-free block.");
        if (prev && GET_ALLOC(HDRP(prev))) PRINT_AND_FAIL("A free block is pointing (prev) to a non-free block.");

        // "Are there any contiguous free blocks that somehow escaped coalescing?"
        // Make sure there is no physical next/prev free blocks from this free
        // blocks, as that would indicate that a block has escaped coalescing.
        physNext = NEXT_BLKP(bp);
        physPrev = PREV_BLKP(bp);
        if (physNext && IS_IN_RANGE(physNext) && physNext != bp && GET_ALLOC(HDRP(physNext)) == 0) PRINT_AND_FAIL("A free block has escaped coalescing, as it has a succeeding free block that could have been coalesced.");
        if (physPrev && IS_IN_RANGE(physPrev) && physPrev != bp && GET_ALLOC(HDRP(physPrev)) == 0) PRINT_AND_FAIL("A free block has escaped coalescing, as it has a preceding free block that could have been coalesced.");


        bp = GET_ADDR(NEXTP(bp));
    }

    // Check the heap list for any free blocks that are not also present in the
    // free list
    bp = heap_listp;
    while (IS_IN_RANGE(bp)) {
        if (!GET_ALLOC(HDRP(bp))) {
            void *freeBp = root;
            while (freeBp && freeBp != bp) freeBp = GET_ADDR(NEXTP(freeBp));
            if (!freeBp) PRINT_AND_FAIL("A free block was found in the heap list that is not also present in the free list.");
        }

        bp = NEXT_BLKP(bp);
    }


    // Return nonzero valaue iff heap is consistent (no inconsistencies found)
    return 1;
}
