/*
 * mm.c - Implicit free-list allocator with safe chunk handling
 *
 * - Uses size_t headers/footers (no block_header struct)
 * - 16-byte alignment
 * - Each mem_map()'d chunk stores metadata at its start so we can detect boundaries
 * - First-fit implicit free list scanning per chunk
 * - Coalescing & splitting inside chunk
 *
 * Notes:
 * - mem_map() is always called with a PAGE_ALIGN(...) size
 * - No global compound static arrays; only scalar globals (chunk_list_head)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "mm.h"
#include "memlib.h"

/* Debug flag */
#define DEBUG 1

/* ---------- Alignment and basic sizes ---------- */
#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

/* size_t header/footer size (aligned) */
#define SIZE_T_SZ (ALIGN(sizeof(size_t)))
#define OVERHEAD (2 * SIZE_T_SZ)            /* header + footer */
#define MIN_BLOCK_SIZE (OVERHEAD + 2 * sizeof(void *)) /* minimum free block payload holds two pointers */

/* Page align helper for mem_map */
#define PAGE_ALIGN(size) (((size) + (mem_pagesize() - 1)) & ~(mem_pagesize() - 1))

/* ---------- Header/footer macros (size_t based) ---------- */
/* Given payload pointer bp, header is SIZE_T_SZ bytes before payload */
#define HDRP(bp) ((char *)(bp) - SIZE_T_SZ)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - OVERHEAD)

/* Given payload pointer bp, next/prev physical blocks payload pointers */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - SIZE_T_SZ)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - OVERHEAD)))

/* Read/write header/footer (stored as size_t) */
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))

/* Pack size and allocation bit */
#define PACK(size, alloc) ((size) | (alloc))

/* Extract size and allocation */
#define GET_SIZE(p) (GET(p) & ~(size_t)(ALIGNMENT - 1))
#define GET_ALLOC(p) (GET(p) & (size_t)0x1)

/* ---------- Chunk metadata layout ----------
   Each mem_map'd chunk starts with CHUNK_META_SIZE bytes:
   [ prev_chunk_ptr (void*) ][ next_chunk_ptr (void*) ][ chunk_size (size_t) ]
   CHUNK_META_SIZE is aligned to ALIGNMENT so payload after it is 16-byte aligned.
*/
#define CHUNK_META_RAW (2 * sizeof(void *) + sizeof(size_t))
#define CHUNK_META_SIZE (ALIGN(CHUNK_META_RAW))
#define CHUNK_PREV(ptr)   (*(void **)((char *)(ptr) + 0))
#define CHUNK_NEXT(ptr)   (*(void **)((char *)(ptr) + sizeof(void *)))
#define CHUNK_SIZE_FIELD(ptr) (*(size_t *)((char *)(ptr) + 2 * sizeof(void *)))

/* ---------- Global state ---------- */
/* Head of linked list of chunks (pointer to chunk start returned by mem_map) */
static void *chunk_list_head = NULL;

/* ---------- Debug printing ---------- */
static void dbg_printf(const char *fmt, ...)
{
#if DEBUG
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
#endif
}

/* ---------- Helper: chunk management ---------- */

/* Insert chunk into chunk_list (front) and store its size */
static void add_chunk(void *chunk_start, size_t chunk_size)
{
    CHUNK_PREV(chunk_start) = NULL;
    CHUNK_NEXT(chunk_start) = chunk_list_head;
    CHUNK_SIZE_FIELD(chunk_start) = chunk_size;

    if (chunk_list_head != NULL) {
        CHUNK_PREV(chunk_list_head) = chunk_start;
    }
    chunk_list_head = chunk_start;
}

/* Return 1 if pointer p lies inside given chunk (payload region) */
static int chunk_contains(void *chunk_start, void *p)
{
    if (chunk_start == NULL || p == NULL) return 0;
    char *start = (char *)chunk_start + CHUNK_META_SIZE; /* first payload byte */
    size_t csize = CHUNK_SIZE_FIELD(chunk_start);
    char *end = (char *)chunk_start + csize; /* one past last byte */
    return ((char *)p >= start) && ((char *)p < end);
}

/* Find the chunk that contains a payload pointer bp (returns chunk_start or NULL) */
static void *find_chunk_for_bp(void *bp)
{
    void *c = chunk_list_head;
    while (c != NULL) {
        if (chunk_contains(c, bp)) return c;
        c = CHUNK_NEXT(c);
    }
    return NULL;
}

/* ---------- Core: block helpers inside a chunk ---------- */

/* create a new chunk big enough to hold req_block_size (which includes overhead).
 * We must map at least mem_pagesize() bytes. The returned chunk stores metadata at its start.
 * Returns 0 on success, -1 on failure.
 */
static int create_chunk_for_block(size_t req_block_size)
{
    size_t pagesz = mem_pagesize();

    /* We need chunk payload (after metadata) to be >= req_block_size.
     * So chunk_size >= CHUNK_META_SIZE + req_block_size.
     * Round up to page size.
     */
    size_t needed = CHUNK_META_SIZE + req_block_size;
    if (needed < pagesz) needed = pagesz;
    size_t chunk_size = PAGE_ALIGN(needed);

    void *chunk = mem_map(chunk_size);
    if (chunk == NULL) return -1;

    /* Write metadata at chunk start */
    add_chunk(chunk, chunk_size);

    /* payload start */
    char *payload = (char *)chunk + CHUNK_META_SIZE;

    /* The initial free block in this chunk spans payload..(chunk_end) */
    size_t block_size = chunk_size - CHUNK_META_SIZE; /* includes header+footer */

    /* place header and footer for the single free block */
    PUT((char *)payload - SIZE_T_SZ, PACK(block_size, 0));                 /* header */
    PUT((char *)payload + block_size - OVERHEAD, PACK(block_size, 0));     /* footer */

    /* place epilogue header at chunk_end - SIZE_T_SZ (one-size header with alloc bit 1 and size 0) */
    PUT((char *)chunk + chunk_size - SIZE_T_SZ, PACK(0, 1));

    dbg_printf("create_chunk: chunk=%p chunk_size=%zu payload=%p block_size=%zu\n",
               chunk, chunk_size, payload, block_size);

    return 0;
}

/* ---------- Finding a fit: scan chunks then blocks (first-fit) ---------- */
static void *find_fit(size_t asize)
{
    void *c = chunk_list_head;
    while (c != NULL) {
        /* scan blocks in this chunk */
        char *bp = (char *)c + CHUNK_META_SIZE; /* payload pointer of first block */
        while (1) {
            size_t h = GET_SIZE((char *)bp - SIZE_T_SZ);
            if (h == 0) break; /* epilogue in this chunk */
            if (!GET_ALLOC((char *)bp - SIZE_T_SZ) && h >= asize) {
                dbg_printf("find_fit: chunk %p found block %p size %zu\n", c, bp, h);
                return bp;
            }
            bp = (char *)bp + h;
            /* ensure we don't step past chunk */
            if (!chunk_contains(c, bp)) break;
        }
        c = CHUNK_NEXT(c);
    }
    return NULL;
}

/* ---------- Coalescing (only within same chunk) ---------- */
static void *coalesce(void *bp)
{
    if (bp == NULL) return NULL;

    void *c = find_chunk_for_bp(bp);
    if (c == NULL) return bp; /* shouldn't happen */

    size_t prev_alloc = 1;
    size_t next_alloc = 1;
    size_t size = GET_SIZE(HDRP(bp));

    /* Prev block */
    void *prev_bp = PREV_BLKP(bp);
    if (chunk_contains(c, prev_bp)) {
        prev_alloc = GET_ALLOC(FTRP(prev_bp));
    }

    /* Next block */
    void *next_bp = NEXT_BLKP(bp);
    if (chunk_contains(c, next_bp)) {
        next_alloc = GET_ALLOC(HDRP(next_bp));
    }

    if (prev_alloc && next_alloc) {
        /* nothing */
    } else if (prev_alloc && !next_alloc) {
        /* merge with next */
        size += GET_SIZE(HDRP(next_bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {
        /* merge with prev */
        size += GET_SIZE(HDRP(prev_bp));
        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(prev_bp), PACK(size, 0));
        bp = prev_bp;
    } else {
        /* merge prev + current + next */
        size += GET_SIZE(HDRP(prev_bp)) + GET_SIZE(HDRP(next_bp));
        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(prev_bp), PACK(size, 0));
        bp = prev_bp;
    }
    return bp;
}

/* ---------- Place: mark allocated and split if leftover big enough ---------- */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    if (csize >= asize + MIN_BLOCK_SIZE) {
        /* split */
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *next_bp = NEXT_BLKP(bp);
        size_t rsize = csize - asize;
        PUT(HDRP(next_bp), PACK(rsize, 0));
        PUT(FTRP(next_bp), PACK(rsize, 0));
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/* ---------- mm_init ---------- */
int mm_init(void)
{
    chunk_list_head = NULL;

    /* Create an initial chunk of one page */
    if (create_chunk_for_block(mem_pagesize() - CHUNK_META_SIZE) == -1)
        return -1;

    return 0;
}

/* ---------- mm_malloc ---------- */
void *mm_malloc(size_t size)
{
    if (size == 0) return NULL;

    /* compute asize: include overhead and alignment, ensure at least MIN_BLOCK_SIZE */
    size_t asize = ALIGN(size + OVERHEAD);
    if (asize < MIN_BLOCK_SIZE) asize = MIN_BLOCK_SIZE;

    /* search for fit */
    void *bp = find_fit(asize);
    if (bp != NULL) {
        place(bp, asize);
        dbg_printf("mm_malloc: returning bp=%p\n", bp);
        return bp;
    }

    /* No fit -> create a new chunk that can hold asize */
    if (create_chunk_for_block(asize) == -1) return NULL;

    /* find again (should succeed) */
    bp = find_fit(asize);
    if (bp == NULL) return NULL;
    place(bp, asize);
    dbg_printf("mm_malloc after extend: returning bp=%p\n", bp);
    return bp;
}

/* ---------- mm_free ---------- */
void mm_free(void *ptr)
{
    if (ptr == NULL) return;

    /* mark free */
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    /* coalesce within chunk */
    void *newbp = coalesce(ptr);
    (void)newbp; /* silent unused in some builds; coalesce updated block in-place */
}

/* ---------- done ---------- */
