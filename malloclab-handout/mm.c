/*
  For my approach I decided to use an explicit free list (implemented via a doubly
  linked list) for keeping track of free blocks. A block itself is header + payload + footer.
  When allocating a block we check if there's an available size in the free list, if there
  is then we remove that block from the list. Blocks are coalesced and split when possible.
  Pages also get unmapped when it can be.
*/
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* always use 16-byte alignment */
#define ALIGNMENT 16

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))

/* rounds up to the nearest multiple of mem_pagesize() */
#define PAGE_ALIGN(size) (((size) + (mem_pagesize()-1)) & ~(mem_pagesize()-1))

#define BLOCK_OVERHEAD 16

// This assumes you have a struct or typedef called "block_header" and "block_footer"
#define OVERHEAD (sizeof(block_header)+sizeof(block_footer))

// Given a payload pointer, get the header or footer pointer
#define HDRP(bp) ((char *)(bp) - sizeof(block_header))
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp))-OVERHEAD)

// Given a payload pointer, get the next or previous payload pointer
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE((char*)(bp)-OVERHEAD))

// ******These macros assume you are using a size_t for headers and footers******
//
// Given a pointer to a header, get or set its value
#define GET(p) (*(size_t*)(p))
#define PUT(p, val) (*(size_t*)(p) = (val))

// Combine a size and alloc bit
#define PACK(size, alloc) ((size) | (alloc))

// Given a header pointer, get the alloc or size
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_SIZE(p) (GET(p) & ~0xF)

#define MAX(x, y) ((x) > (y) ? (x):(y))

// ========= Linked List Functions ========

typedef struct node{
    struct node *next;
    struct node *prev;
} node;

typedef size_t block_header;
typedef size_t block_footer;
size_t multiplier = 2;


node* head = NULL;

/*
  Generic add node linked list function. Adds to front
*/
void add_node(node* new){
    if(head == NULL){
        head = new;
        head->next = NULL;
        head->prev = NULL;
    }
    else{
        head->prev = new;
        new->next = head;
        head = new;
        head->prev = NULL;
    }
}
/*
  Gerneric remove node linked list function
*/
void remove_node(node* curr){
    node* prev = curr->prev;
    node* next = curr->next;

    if(curr == head) {
        head = next;
    }

    if(prev != NULL){
        prev->next = next;
    }

    if(next != NULL){
        next->prev = prev;
    }

    return;
}

/*
  Finds a block in the linked list that is big enough
*/
node* find_node(size_t requested_size){
    node* current;
    current = head;

    while(current != NULL){
        // if the current block big enough
        if(GET_SIZE(HDRP((void*)(current))) >= requested_size) {
            remove_node(current);
            return current;
        }
        // not big enough
        current = current->next;
    }
    return NULL;
}

// ======= malloc ======

/* 
  Request additionaly memory with mem_map.
  Initialize the new chunk of memory as applicable
  Update free list if applicable
 */
static void* extend(size_t s){
    size_t requested_size = multiplier * PAGE_ALIGN(s+(OVERHEAD * 2));
    if(multiplier < 16)
        multiplier++;
    void* new_page = mem_map(requested_size);

    size_t* page_words = (size_t*)new_page;
    size_t num_words = requested_size / sizeof(size_t);

    // Page prolog and terminator
    page_words[0] = 0x1;
    page_words[1] = PACK(BLOCK_OVERHEAD, 1);
    page_words[2] = PACK(BLOCK_OVERHEAD, 1);
    page_words[num_words - 1] = PACK(0, 1);

    // The new free block is the page body.
    // Set the new free block's header and footer:
    size_t free_block_size = requested_size - 4 * sizeof(size_t);
    page_words[3] = PACK(free_block_size, 0);
    page_words[num_words - 2] = PACK(free_block_size, 0);

    return (void*)&page_words[4];
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    multiplier = 2;
    head = NULL;

    extend(4090);
    return 0;
}

/* 
 * Set a block to allocated
 * Update block headers/footers as needed
 * Update free list if applicable
 * Split block if applicable
 */
static void* set_allocated(void* b, size_t size){
    size_t prev_size = GET_SIZE(HDRP(b));
    size_t remaining_size = prev_size - size;
    
    remove_node((node*)(b));

    if(remaining_size < OVERHEAD + sizeof(node)){
        PUT(HDRP(b), PACK(GET_SIZE(HDRP(b)), 1));
        PUT(FTRP(b), PACK(GET_SIZE(FTRP(b)), 1));
    }
    else{
        PUT(HDRP(b),PACK(size, 1));
        PUT(FTRP(b),PACK(size, 1));

        void* new_header = NEXT_BLKP(b);
        PUT(HDRP(new_header), PACK(remaining_size, 0));
        PUT(FTRP(new_header), PACK(remaining_size, 0));

        add_node((node*)(new_header));
    }

    return b;
}

/* 
 * mm_malloc - Allocate a block by using bytes from current_avail,
 *     grabbing a new page if necessary.
 */
void *mm_malloc(size_t size)
{
    size_t maxsize = MAX(size, sizeof(node));
    size_t newsize = ALIGN(maxsize + BLOCK_OVERHEAD);

    node* block;
    block = find_node(newsize);

    if(block == NULL){ // couldn't find anything of size
        block = extend(newsize);
        set_allocated(block, newsize);
    }
    else{
        block = set_allocated((void*)(block), newsize);
    }
    
    return (void*)(block);
}

/* 
  Coalesce a free block if applicable
  Returns pointer to new coalesced block
*/
static void* coalesce(void* bp){
    size_t ptr_size = GET_SIZE(HDRP(bp));
    
    void* new_block = bp;

    void* left = PREV_BLKP(bp);
    void* right = NEXT_BLKP(bp);

    size_t left_alloc = GET_ALLOC(HDRP(left));
    size_t right_alloc = GET_ALLOC(HDRP(right));

    // When both neighbors are free
    if(!left_alloc && !right_alloc){
        size_t new_size = GET_SIZE(HDRP(left)) + ptr_size + GET_SIZE(HDRP(right));
        remove_node((node*)(right));
        PUT(HDRP(left), PACK(new_size, 0));
        PUT(FTRP(left), PACK(new_size, 0));
        new_block = left;
    }

    // When the left neigbor is free
    else if(!left_alloc && right_alloc){
        size_t new_size = GET_SIZE(HDRP(left)) + ptr_size;
        PUT(HDRP(left), PACK(new_size, 0));
        PUT(FTRP(left), PACK(new_size, 0));
        new_block = left;
    }

    // When the right neighbor is free
    else if(left_alloc && !right_alloc){
        size_t new_size = ptr_size + GET_SIZE(HDRP(right));
        remove_node((node*)(right));
        add_node((node*)(bp));
        PUT(HDRP(bp), PACK(new_size, 0));
        PUT(FTRP(bp), PACK(new_size, 0));
    }

    // When neither neighbor is free
    else{
        PUT(HDRP(bp), PACK(ptr_size, 0));
        PUT(FTRP(bp), PACK(ptr_size, 0));

        add_node((node*)(bp));
    }

    return new_block;
}

/*
  hecks if you can unmap the page
*/
static void unmap_page(void* ptr){
    void* left = HDRP(PREV_BLKP(ptr));
    void* right = HDRP(NEXT_BLKP(ptr));

    // check if the size of the previous is overhead and the size of the next is 0
    if(GET_SIZE(left) == OVERHEAD && GET_SIZE(right) == 0){
        size_t page_size = PAGE_ALIGN(GET_SIZE(HDRP(ptr)));
        remove_node((node*)(ptr));
        mem_unmap((ptr-BLOCK_OVERHEAD*2), page_size);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    ptr = coalesce(ptr);
    unmap_page(ptr);
}