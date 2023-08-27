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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 头部/脚部的大小 */
#define WSIZE 4
/* 双字 */
#define DSIZE 8

/* 扩展堆时的默认大小 */
#define CHUNKSIZE (1 << 12)
#define MAX_BLK_SIZE (~0x7)

/* 设置头部和脚部的值, 块大小+分配位 */
#define PACK(size, alloc) ((size) | (alloc))

/* 读写指针p的位置 */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) ((*(unsigned int *)(p)) = (val))

/* 从头部或脚部获取大小或分配位 */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* 给定有效载荷指针, 找到头部和脚部 */
#define HDRP(bp) ((char*)(bp) - WSIZE)
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* 给定有效载荷指针, 找到前一块或下一块 */
#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(((char*)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE(((char*)(bp) - DSIZE)))

#define SET_SUCC(bp, pred) PUT(bp, pred)
#define SET_PRED(bp, succ) PUT(((char*)(bp) + WSIZE), succ)
#define SUCC_FRBLKP(bp) ((void*)GET(bp))
#define PRED_FRBLKP(bp) ((void*)GET((char*)(bp) + WSIZE))

#define LIST_SIZE 9

#define IS_HEAD_ALLOC(bp) GET_ALLOC(HDRP(bp)) 
#define IS_FOOTER_ALLOC(bp) GET_ALLOC(FTRP(bp))
#define IS_ALLOC(bp) (IS_HEAD_ALLOC(bp) && IS_FOOTER_ALLOC(bp))
#define IS_HEAP_END(bp) (IS_HEAD_ALLOC(bp) && (GET_SIZE(HDRP(bp)) == 0))
#define IS_SIZE_VALUABLE(bp) ((GET_SIZE(HDRP(bp)) >= (4*WSIZE)) && (GET_SIZE(FTRP(bp)) >= (4*WSIZE)))
#define GET_BLKP_AT(bp, num) ((void*)(((char*)(bp)) + ((num) * WSIZE)))

#define DEBUG 0

#if DEBUG
#define DEBUG_PRINT(fmt, ...) fprintf(stderr, "DEBUG: %s:%d: " fmt "\n", __func__, __LINE__,  ##__VA_ARGS__)
#define HEAP_CHECK 
#else
#define  DEBUG_PRINT(fmt, ...)
#define HEAP_CHECK
#endif



void* heap_list = NULL;
void* block_list = NULL;

inline void get_min_max_block(size_t* min, size_t* max, int row)
{
    if(row < 0 || row >= LIST_SIZE){
        DEBUG_PRINT("row has to be in [0, %d)", LIST_SIZE);
        exit(1);
    }
    if(row == 0){
        *min = 0;
        *max = 32;
    }
    else if(row == LIST_SIZE - 1){
        *min = 1 << (5 + row - 1);
        *max = MAX_BLK_SIZE;
    }
    else{
        *min = 1 << (5 + row - 1);
        *max = 1 << (5 + row);
    }
}

void block_list_check()
{
    if(block_list == NULL){
        DEBUG_PRINT("heap_list is NULL");
        return;
    }
    for(int i = 0; i < LIST_SIZE; i++){      
        void* p = SUCC_FRBLKP(block_list + (i * WSIZE));
        int col = 0;
        size_t min = 0;
        size_t max = 0;
        get_min_max_block(&min, &max, i);
        while(p != NULL){
            col++;
            if(GET_ALLOC(HDRP(p))){
                DEBUG_PRINT("block %x header in list row %d is allocated", *((unsigned int*)p), i);
            }
            if(GET_ALLOC(FTRP(p))){
                DEBUG_PRINT("block %x footer in list row %d is allocated", *((unsigned int*)p), i);
            }
            size_t head_size = GET_SIZE(HDRP(p));
            size_t foot_size = GET_SIZE(FTRP(p));
            if(head_size <= min || head_size > max){
                DEBUG_PRINT("block %x in list row %d head size %zu not in (%zu, %zu]", *((unsigned int*)p), i, head_size, min, max);
            }
            if(foot_size <= min || foot_size > max){
                DEBUG_PRINT("block %x in list row %d footer size %zu not in (%zu, %zu]", *((unsigned int*)p), i, foot_size, min, max);
            }
            if(head_size != foot_size){
                DEBUG_PRINT("block %x in list row %d head size %zu doesn't equalt to footer size %zu", *((unsigned int*)p), i, head_size, foot_size);
            }
            void* next_addr = SUCC_FRBLKP(p);
            void* prev_addr = PRED_FRBLKP(p);
            if(next_addr == prev_addr){
                DEBUG_PRINT("block %x in list row %d head size %zu footer size %zu has the same prev next addr 0x%x", *((unsigned int*)p), i, head_size, foot_size, prev_addr);
            }
            p = SUCC_FRBLKP(p);
        }
        DEBUG_PRINT("list row %d has %d cols", i, col);
    }  
}

inline void* get_block_first(size_t size){
    for(int i = 0; i < LIST_SIZE; i++){
        size_t min;
        size_t max;
        get_min_max_block(&min, &max, i);
        if( min<size && size <= max ){
            void* bp_in_list = GET_BLKP_AT(block_list, i);
            return bp_in_list;
        }
    }
    return NULL;
}

inline int is_block_in_list(void* p)
{
    void* block_first;
    size_t size = GET_SIZE(HDRP(p));
    if((block_first = get_block_first(size)) == NULL)
        return 0;
    void* bp_in_list = SUCC_FRBLKP(block_first);
    DEBUG_PRINT("p is 0x%x, size is %d, block_first is 0x%x, SUCC is 0x%x", p, size, block_first, SUCC_FRBLKP(block_first));
    while(bp_in_list != NULL){
        DEBUG_PRINT("in while, bp_in_list is 0x%x, SUCC is 0x%x , pred is 0x%x", bp_in_list, SUCC_FRBLKP(bp_in_list), PRED_FRBLKP(bp_in_list));
        if(bp_in_list == p){
            return 1;
        }
        bp_in_list = SUCC_FRBLKP(bp_in_list);
    }
    return 0;
}

void heap_list_check()
{
    char* bp = NEXT_BLKP(heap_list);
    char* bpstep2 = NULL;
    if(!IS_HEAP_END(bp)){
        bpstep2 = NEXT_BLKP(bp);
    }
    DEBUG_PRINT("block_list 0x%x, heap_list 0x%x", block_list, heap_list);
    while(!IS_HEAP_END(bp)){

        DEBUG_PRINT("bp is 0x%x, size is %d, alloc is %d, succ is 0x%x, pred is 0x%x", bp, GET_SIZE(HDRP(bp)), IS_HEAD_ALLOC(bp), SUCC_FRBLKP(bp), PRED_FRBLKP(bp));
        if(bpstep2 != NULL){
            DEBUG_PRINT("bpstep2 is 0x%x, size is %d, alloc is %d", bpstep2, GET_SIZE(HDRP(bpstep2)), IS_HEAD_ALLOC(bpstep2));
        }

        if(!IS_SIZE_VALUABLE(bp)){
            DEBUG_PRINT("block %x size %d not valuable", GET(bp), GET_SIZE(HDRP(bp)));
            exit(1);
        }
        if(!IS_ALLOC(bp)){
            if((!IS_HEAP_END(NEXT_BLKP(bp))) && (!IS_ALLOC(NEXT_BLKP(bp)))){
                DEBUG_PRINT("block %x and %x are continue free blocks", GET(bp), GET(NEXT_BLKP(bp)));
                exit(1);
            }
            if(!is_block_in_list(bp)){
                DEBUG_PRINT("free block %x is not in list", GET(bp));
                exit(1);
            }
        }
        else if(is_block_in_list(bp)){
            DEBUG_PRINT("block %x is not free but in list", GET(bp));
            exit(1);
        }
        else if((bpstep2 != NULL) && (!IS_HEAP_END(bpstep2)) && bpstep2 == bp){
            DEBUG_PRINT("block %x is in forever loop", bp);
        }
        bp = NEXT_BLKP(bp);
        if((bpstep2 != NULL) && (!IS_HEAP_END(bpstep2)) && (!IS_HEAP_END(NEXT_BLKP(bpstep2))) ){
            bpstep2 = NEXT_BLKP(NEXT_BLKP(bpstep2));
        }
        else{
            bpstep2 = NULL;
        }
    } 
}

void heap_check()
{
    block_list_check();
    heap_list_check();
}

inline static void remove_block_from_list(void* bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    void* block_first = get_block_first(size);
    if(block_first == NULL){
        DEBUG_PRINT("in remove_block_from_list: there is no block first found");
    }
    void* next = SUCC_FRBLKP(block_first);
    DEBUG_PRINT("bp is 0x%x, block_first is 0x%x, next is 0x%x", bp, block_first, next);
    while(next != NULL){
        if(next == bp){
            SET_SUCC(PRED_FRBLKP(next), SUCC_FRBLKP(next));
            if(SUCC_FRBLKP(next) != NULL){
                SET_PRED(SUCC_FRBLKP(next), PRED_FRBLKP(next));
            }
            break;
        }
        next = SUCC_FRBLKP(next);
        DEBUG_PRINT("next is 0x%x", next);
    }
}

inline static void put_block_in_list(void* bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    void* block_first = get_block_first(size);
    if(block_first == NULL) 
    {
        DEBUG_PRINT("put_block_in_list: there is no block first found");
    }
    void* next = SUCC_FRBLKP(block_first);
    SET_SUCC(block_first, bp);
    SET_PRED(bp, block_first);
    SET_SUCC(bp, next);
    if(next != NULL){
        SET_PRED(next, bp);
    }
    DEBUG_PRINT("in put");
    HEAP_CHECK;
}

static void* coalesce(void* bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));     /* 前一块大小 */
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));     /* 后一块大小 */
    size_t size = GET_SIZE(HDRP(bp));                       /* 当前块大小 */
    /*
     * 四种情况：前后都不空, 前不空后空, 前空后不空, 前后都空
     */
    /* 前后都不空 */
    if(prev_alloc && next_alloc){
        DEBUG_PRINT("after prev_alloc && next_alloc");
        put_block_in_list(bp);
        return bp;
    }
    /* 前不空后空 */
    else if(prev_alloc && !next_alloc){
        remove_block_from_list(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));  //增加当前块大小
        PUT(HDRP(bp), PACK(size, 0));           //先修改头
        PUT(FTRP(bp), PACK(size, 0));           //根据头部中的大小来定位尾部
        
    }
    /* 前空后不空 */
    else if(!prev_alloc && next_alloc){
        remove_block_from_list(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));  //增加当前块大小
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);                     //注意指针要变
    }
    /* 都空 */
    else{
        remove_block_from_list(PREV_BLKP(bp));
        remove_block_from_list(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));  //增加当前块大小
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    DEBUG_PRINT("coalesce: after  else");
    put_block_in_list(bp);
    return bp;
}

inline static void *extend_heap(size_t words)
{
    char* bp;
    size_t size;
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if((long)(bp = mem_sbrk(size)) == -1){
        return NULL;
    }

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    DEBUG_PRINT("word is %d size is %d in extend_heap before coalesce", words, size);
    return coalesce(bp);
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if((heap_list = mem_sbrk((3 + LIST_SIZE)*WSIZE)) == (void *)-1)
        return -1;
    for(int i = 0; i < LIST_SIZE; i++){
        PUT(heap_list + (i * WSIZE), 0);
    }
    PUT(heap_list + ((LIST_SIZE + 0) * WSIZE), PACK(DSIZE, 1));
    PUT(heap_list + ((LIST_SIZE + 1) * WSIZE), PACK(DSIZE, 1));
    PUT(heap_list + ((LIST_SIZE + 2) * WSIZE), PACK(0, 1));
    block_list = heap_list;
    heap_list = heap_list + (WSIZE * (LIST_SIZE + 1));
    if(extend_heap(CHUNKSIZE / WSIZE) == NULL){
        return -1;
    }
    return 0;
}

inline char* find_fit(size_t size){
    void* block_first;
    for(int i = 0; i < LIST_SIZE; i++){
        size_t min;
        size_t max;
        get_min_max_block(&min, &max, i);
        if(size > max) continue;
        void* bp_in_list = GET_BLKP_AT(block_list, i);
        void* next = SUCC_FRBLKP(bp_in_list);
        while(next != NULL){
            if(GET_SIZE(HDRP(next)) < size){
                next = SUCC_FRBLKP(next);
                continue;
            }
            return (char*)next;
        }
    }
    return NULL;
}

inline void place(char* bp, size_t asize)
{
    size_t oldsize = GET_SIZE(HDRP(bp));
    if((asize + 4 * DSIZE) <= oldsize){
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        PUT(HDRP(NEXT_BLKP(bp)), PACK(oldsize - asize, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(oldsize - asize, 0));
        SET_SUCC(PRED_FRBLKP(bp), SUCC_FRBLKP(bp));
        if(SUCC_FRBLKP(bp) != NULL){
            SET_PRED(SUCC_FRBLKP(bp), PRED_FRBLKP(bp));
        }  
        DEBUG_PRINT("place: after asize + 4 * DSIZE"); 
        put_block_in_list(NEXT_BLKP(bp));
    }
    else{
        PUT(HDRP(bp), PACK(oldsize, 1));
        PUT(FTRP(bp), PACK(oldsize, 1));
        SET_SUCC(PRED_FRBLKP(bp), SUCC_FRBLKP(bp));
        if(SUCC_FRBLKP(bp) != NULL){
            SET_PRED(SUCC_FRBLKP(bp), PRED_FRBLKP(bp));
        }     
    }
    DEBUG_PRINT("place: in place");
    HEAP_CHECK;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char* bp;
    if(size == 0){
        return NULL;
    }
    if(size < DSIZE){
        asize = 2 * DSIZE; 
    }
    else{
        asize = ALIGN(size + DSIZE);
    }

    if((bp = find_fit(asize)) != NULL){
        place(bp, asize);
        return bp;
    }
    extendsize = asize > CHUNKSIZE ? asize : CHUNKSIZE;
    if((bp = extend_heap(extendsize / WSIZE)) == NULL){
        return NULL;
    }
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    DEBUG_PRINT("in mm_free before coalesce");
    coalesce(bp);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    if(ptr == NULL){
        return mm_malloc(size);
    }
    if(size == 0){
        mm_free(ptr);
        return NULL;
    }

    DEBUG_PRINT("size: %zu", size);
    void *oldptr = ptr;
    void *newptr;
    size_t oldSize, newSize, copySize; 
    newSize = ALIGN(size) + DSIZE;
    oldSize = GET_SIZE(HDRP(ptr));

    if(oldSize >= (DSIZE * 2 + newSize)){ 
        DEBUG_PRINT("oldsize: %zu, newsize: %zu, ptr: 0x%x, next: 0x%x", oldSize, newSize, ptr, NEXT_BLKP(ptr));
        PUT(HDRP(ptr), PACK(newSize, 1));
        PUT(FTRP(ptr), PACK(newSize, 1));
        PUT(HDRP(NEXT_BLKP(ptr)), PACK(oldSize - newSize, 0));
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(oldSize - newSize, 0));
        put_block_in_list(NEXT_BLKP(ptr));
        return ptr;
    }

    if(oldSize >= newSize){
        DEBUG_PRINT("oldsize: %zu, ptr: 0x%x", oldSize, ptr);
        return ptr;
    }
    
    void* next = NEXT_BLKP(ptr);
    if((!IS_HEAP_END(next)) && (!IS_ALLOC(next))){
        size_t addSize = GET_SIZE(HDRP(next)) + oldSize;
        DEBUG_PRINT("oldsize: %zu, addsize: %zu, ptr: 0x%x, next: 0x%x", oldSize, addSize, ptr, NEXT_BLKP(ptr));
        if(addSize >= (DSIZE * 2 + newSize)){
            remove_block_from_list(next);
            PUT(HDRP(ptr), PACK(newSize, 1));
            PUT(FTRP(ptr), PACK(newSize, 1));
            PUT(HDRP(NEXT_BLKP(ptr)), PACK(addSize - newSize, 0));
            PUT(FTRP(NEXT_BLKP(ptr)), PACK(addSize - newSize, 0));
            put_block_in_list(NEXT_BLKP(ptr));
            DEBUG_PRINT("ptr: 0x%x, next: 0x%x", ptr, NEXT_BLKP(ptr));
            return ptr;
        }

        if(addSize >= newSize){
            remove_block_from_list(next);
            PUT(HDRP(ptr), PACK(addSize, 1));
            PUT(FTRP(ptr), PACK(addSize, 1));
            DEBUG_PRINT("ptr: 0x%x, next: 0x%x", ptr, NEXT_BLKP(ptr));
            return ptr;
        }
    }

    void* prev = PREV_BLKP(ptr);
    if((prev != heap_list) && (!IS_ALLOC(prev))){
        size_t addSize = GET_SIZE(HDRP(prev)) + oldSize;
        DEBUG_PRINT("oldsize: %zu, addsize: %zu, ptr: 0x%x, next: 0x%x", oldSize, addSize, ptr, PREV_BLKP(ptr));
        if((addSize >= (DSIZE * 2 + newSize)) && (GET_SIZE((HDRP(prev))) >= (oldSize - DSIZE))){
            remove_block_from_list(prev);
            memcpy(prev, ptr, oldSize - DSIZE);
            DEBUG_PRINT("prev: 0x%x, ptr: 0x%x, oldSize - DSIZE: %zu, addsize: %zu, ptrsize: %zu", prev, ptr, oldSize - DSIZE, addSize, oldSize);
            PUT(HDRP(prev), PACK(newSize, 1));
            PUT(FTRP(prev), PACK(newSize, 1));
            PUT(HDRP(NEXT_BLKP(prev)), PACK(addSize - newSize, 0));
            PUT(FTRP(NEXT_BLKP(prev)), PACK(addSize - newSize, 0));
            put_block_in_list(NEXT_BLKP(prev));
            DEBUG_PRINT("prev_ptr: 0x%x, next: 0x%x", ptr, NEXT_BLKP(prev));
            return prev;
        }

        if((addSize >= newSize) && (GET_SIZE((HDRP(prev))) >= (oldSize - DSIZE))){
            remove_block_from_list(prev);
            memcpy(prev, ptr, oldSize - DSIZE);
            DEBUG_PRINT("prev: 0x%x, ptr: 0x%x, oldSize - DSIZE: %zu, addsize: %zu, ptrsize: %zu", prev, ptr, oldSize - DSIZE, addSize, oldSize);
            PUT(HDRP(prev), PACK(addSize, 1));
            PUT(FTRP(prev), PACK(addSize, 1));
            DEBUG_PRINT("prev_ptr: 0x%x, next: 0x%x", ptr, NEXT_BLKP(prev));
            return prev;
        }
    }

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = oldSize - DSIZE;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}
