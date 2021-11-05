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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

//#define DEBUG
#ifdef DEBUG
int malloc_cnt = 0;
int free_cnt = 0;
int realloc_cnt = 0;
int extend_heap_cnt = 1;
#endif

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "myteam",
    /* First member's full name */
    "stopire",
    /* First member's email address */
    "stopire@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define PAGE_SIZE_ORDER 10
#define PAGE_SIZE (1 << PAGE_SIZE_ORDER)
#define MAX_ORDER 11
#define CHUNK_SIZE ((1 << (MAX_ORDER - 1)) * PAGE_SIZE)

void *HEAP_START;
#define BLK_LIST_HEAD(i) (((struct HEAP_HDR *)HEAP_START)->blk_list_head[i])
#define PAGE_LIST_HEAD(i) (((struct HEAP_HDR *)HEAP_START)->page_list_head[i])

#define PAGE_LIST_TYPES_CNT (PAGE_SIZE_ORDER - 3)
struct HEAP_HDR {
  void *blk_list_head[MAX_ORDER];
  void *page_list_head[PAGE_LIST_TYPES_CNT]; // 8, 16, 32, 64, 128, 256, THRES
} __attribute__((aligned(ALIGNMENT)));

struct BLK_HDR {
  int8_t order;
  int8_t allocate;
  struct BLK_HDR *nxt;
} __attribute__((aligned(ALIGNMENT)));

#define GETBITMAP(p, i) ((p->bitmap[i / 8]) & (1 << (i % 8)))
#define SETBITMAP(p, i) (p->bitmap[i / 8] |= (1 << (i % 8)))
#define CLRBITMAP(p, i) (p->bitmap[i / 8] &= ~(1 << (i % 8)))
// assume PAGE_SIZE = 1024
struct PAGE_HDR {
  struct BLK_HDR blk_hdr;

  int16_t alloc_cnt;
  int16_t max_unit;
  int16_t size;
  int8_t bitmap[15]; // at most 120 8-byte allocation in one page
} __attribute__((aligned(ALIGNMENT)));
#define GET_PAGE_HDR(p)                                                        \
  ((struct PAGE_HDR *)(p - ((p - (HEAP_START + sizeof(struct HEAP_HDR))) &     \
                            (PAGE_SIZE - 1))))

#define THRES ((PAGE_SIZE - sizeof(struct PAGE_HDR)) / 2)
/* --------------- buddy system helper function --------------- */

/*
 *  get_blk_by_order - 获取指定order的blk，可能需要拆分更大的blk甚至sbrk
 */
void *get_blk_by_order(int8_t order) {
  int8_t cur_order = order;
  while (cur_order < MAX_ORDER) {
    if (BLK_LIST_HEAD(cur_order)) {
      struct BLK_HDR *blk_to_divide =
          (struct BLK_HDR *)BLK_LIST_HEAD(cur_order);
      blk_to_divide->allocate = 1;
      blk_to_divide->order = order;
      BLK_LIST_HEAD(cur_order) = blk_to_divide->nxt;
      blk_to_divide->nxt = NULL;

      for (int8_t i = order; i < cur_order; i++) {
        void *ptr = (void *)blk_to_divide + PAGE_SIZE * (1 << i);
        struct BLK_HDR *new_blk_hdr = (struct BLK_HDR *)ptr;
        new_blk_hdr->nxt = (struct BLK_HDR *)BLK_LIST_HEAD(i);
        new_blk_hdr->order = i;
        new_blk_hdr->allocate = 0;
        BLK_LIST_HEAD(i) = (void *)new_blk_hdr;
      }

      return (void *)blk_to_divide;
    }
    ++cur_order;
  }

  /* no larger block, sbrk is needed to extend heap */
  BLK_LIST_HEAD(MAX_ORDER - 1) = mem_sbrk(CHUNK_SIZE);
#ifdef DEBUG
  printf("%d times of sbrk, heap_lo:%p, new heap_hi:%p, heap_size=%d\n",
         ++extend_heap_cnt, mem_heap_lo(), mem_heap_hi(), mem_heapsize());
#endif
  return get_blk_by_order(order);
}

/*
 *  get_order - 获取大小为size的内存请求对应的blk order
 */
int get_order(size_t size) {
  size_t tot = size + sizeof(struct BLK_HDR);
  int8_t order = 0;
  while ((1 << order) * PAGE_SIZE < tot)
    ++order;
  return order;
}

/*
 *  is_blk_addr - 判断ptr是否指向一段分配在块上的内存。
 */
int is_blk_addr(void *ptr) {
  struct BLK_HDR *blk_start = (struct BLK_HDR *)(ptr - sizeof(struct BLK_HDR));
  if (blk_start->allocate != 1) return 0;

  // 判断blk_start是不是块的起始地址，即是否与第一个块相差PAGE_SIZE的倍数。
  off_t offset = (void *)blk_start - (HEAP_START + sizeof(struct HEAP_HDR));
  if (offset & (PAGE_SIZE - 1)) return 0;

  return 1;
}

/*
 *  get_buddy_addr - 获得blk所对应的buddy blk的地址
 */
struct BLK_HDR *get_buddy_addr(struct BLK_HDR *blk_start) {
  void *first_blk = HEAP_START + sizeof(struct HEAP_HDR);
  int offset = (void *)blk_start - first_blk;
  offset ^= ((1 << blk_start->order) * PAGE_SIZE);
  return (struct BLK_HDR *)(first_blk + offset);
}

/*
 *  remove_blk_from_list - 将blk从free list中移除
 */
void remove_blk_from_list(struct BLK_HDR *blk_start) {
  if (BLK_LIST_HEAD(blk_start->order) == (void *)blk_start) {
    BLK_LIST_HEAD(blk_start->order) = (void *)(blk_start->nxt);
    blk_start->nxt = NULL;
  } else {
    struct BLK_HDR *p1 = (struct BLK_HDR *)(BLK_LIST_HEAD(blk_start->order));
    struct BLK_HDR *p2 = p1->nxt;
    while (p2 != blk_start) {
      p1 = p2;
      p2 = p2->nxt;
    }
    p1->nxt = p2->nxt;
    p2->nxt = NULL;
  }
}

/*
 *  coalesce - 尝试递归地将blk_start指向的blk与其buddy合并，并插入链表
 */
void coalesce(struct BLK_HDR *blk_start) {
  struct BLK_HDR *buddy = get_buddy_addr(blk_start);

  //合并不了或者已达MAX_ORDER，插链表
  if (blk_start->order == MAX_ORDER - 1 || buddy->allocate ||
      buddy->order != blk_start->order) {
    blk_start->nxt = (struct BLK_HDR *)BLK_LIST_HEAD(blk_start->order);
    BLK_LIST_HEAD(blk_start->order) = (void *)blk_start;
  } else {
    remove_blk_from_list(buddy);

    if (blk_start > buddy) {
      struct BLK_HDR *t = blk_start;
      blk_start = buddy;
      buddy = t;
    }

    //后一个块是大块的一部分，清空其hdr
    memset(buddy, 0, sizeof(struct BLK_HDR));

    ++blk_start->order;

    coalesce(blk_start);
  }
}

/* ---------------end of buddy system helper function --------------- */

/* ---------------inner page malloc helper function --------------- */

/*
 * get_page_list_index - 根据size获得其应位于哪个slab
 */
int get_page_list_index(size_t s) {
  int p = ALIGNMENT;
  int cnt = 0;
  while (p < s) {
    p <<= 1;
    ++cnt;
  }
  // return p > THRES ? (PAGE_LIST_TYPES_CNT - 1) : cnt;
  return cnt;
}

int16_t get_max_unit_by_index(int index) {
  if (index == PAGE_LIST_TYPES_CNT - 1) return 2;
  else if (index == 0)
    return 8 * sizeof(((struct PAGE_HDR *)PAGE_LIST_HEAD(0))->bitmap);
  else
    return (PAGE_SIZE - sizeof(struct PAGE_HDR)) / (1 << (3 + index));
}

int16_t get_size_by_index(int index) {
  if (index == PAGE_LIST_TYPES_CNT - 1) return THRES;
  else
    return ALIGNMENT * (1 << index);
}

/*
 * malloc_in_free_page - 在slab中分配内存，可能需要向buddy system获取页
 */
struct PAGE_HDR *malloc_in_free_page(int index) {
  struct PAGE_HDR *p_hdr = (struct PAGE_HDR *)PAGE_LIST_HEAD(index);
  if (p_hdr == NULL) {
    p_hdr = (struct PAGE_HDR *)get_blk_by_order(0); //从buddy system获取1页
    p_hdr->size = get_size_by_index(index);
    p_hdr->alloc_cnt = 0;
    p_hdr->max_unit = get_max_unit_by_index(index);
    memset(p_hdr->bitmap, 0, sizeof(p_hdr->bitmap));

    PAGE_LIST_HEAD(index) = (void *)p_hdr;
  }
  if (p_hdr->alloc_cnt == p_hdr->max_unit) printf("It's impossible\n");
  for (int i = 0; i < p_hdr->max_unit; i++) {
    if (!GETBITMAP(p_hdr, i)) {
      SETBITMAP(p_hdr, i);
      ++p_hdr->alloc_cnt;
      if (p_hdr->alloc_cnt == p_hdr->max_unit) {
        PAGE_LIST_HEAD(index) = (void *)(p_hdr->blk_hdr.nxt);
        p_hdr->blk_hdr.nxt = NULL;
      }
      return (void *)p_hdr + sizeof(struct PAGE_HDR) + i * p_hdr->size;
    }
  }

  // control never reaches here
#ifdef DEBUG
  printf("size=%d:%d/%d\n", p_hdr->size, p_hdr->alloc_cnt, p_hdr->max_unit);
#endif
  return NULL;
}

/*
 *  remove_page_from_list - 从free page list里删除页
 */
void remove_page_from_list(struct PAGE_HDR *page_start) {
  int index = get_page_list_index(page_start->size);
  if (PAGE_LIST_HEAD(index) == (void *)page_start) {
    PAGE_LIST_HEAD(index) = (void *)(page_start->blk_hdr.nxt);
    page_start->blk_hdr.nxt = NULL;
  } else {
    struct BLK_HDR *p1 = (struct BLK_HDR *)(PAGE_LIST_HEAD(index));
    struct BLK_HDR *p2 = p1->nxt;
    while (p2 != (struct BLK_HDR *)page_start) {
      p1 = p2;
      p2 = p2->nxt;
    }
    p1->nxt = p2->nxt;
    p2->nxt = NULL;
  }
}

/* ---------------end of inner page malloc helper function --------------- */

/* ---------------realloc helper function --------------- */

/*
 *  get_malloc_size - 获取为ptr指向内存段分配的空间
 */

size_t get_malloc_size(void *ptr) {
  if (is_blk_addr(ptr)) {
    struct BLK_HDR *blk_start =
        (struct BLK_HDR *)(ptr - sizeof(struct BLK_HDR));
    return (1 << (blk_start->order)) * PAGE_SIZE;
  } else {
    return GET_PAGE_HDR(ptr)->size;
  }
}

/* ---------------end of realloc helper function --------------- */

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
  HEAP_START = mem_sbrk(CHUNK_SIZE + sizeof(struct HEAP_HDR));
  struct HEAP_HDR *p = (struct HEAP_HDR *)HEAP_START;
  for (int8_t i = 0; i < MAX_ORDER - 1; i++)
    p->blk_list_head[i] = NULL;
  p->blk_list_head[MAX_ORDER - 1] = HEAP_START + sizeof(struct HEAP_HDR);

  struct BLK_HDR *bs = (struct BLK_HDR *)(p->blk_list_head[MAX_ORDER - 1]);
  bs->allocate = 0;
  bs->nxt = NULL;
  bs->order = MAX_ORDER - 1;

  for (int i = 0; i < PAGE_LIST_TYPES_CNT; i++)
    p->page_list_head[i] = NULL;

  return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size) {
  void *ret = NULL;

  if (size > THRES) {
    ret = get_blk_by_order(get_order(size)) + sizeof(struct BLK_HDR);
  } else {
    ret = malloc_in_free_page(get_page_list_index(size));
  }

#ifdef DEBUG
  printf("%d times of malloc, size=%d, ptr=%p\n", ++malloc_cnt, size, ret);
#endif
  return ret;
}

/*
 * mm_free - Freeing a block.
 */
void mm_free(void *ptr) {
#ifdef DEBUG
  printf("%d times of free, ptr=%p\n", ++free_cnt, ptr);
#endif

  if (is_blk_addr(ptr)) { // 由buddy system分配
    struct BLK_HDR *blk_start =
        (struct BLK_HDR *)(ptr - sizeof(struct BLK_HDR));
    blk_start->allocate = 0;

    coalesce(blk_start);
  } else {
    struct PAGE_HDR *p_hdr = GET_PAGE_HDR(ptr);
    int i = (ptr - ((void *)p_hdr + sizeof(struct PAGE_HDR))) / p_hdr->size;
    CLRBITMAP(p_hdr, i);
    --p_hdr->alloc_cnt;

    if (p_hdr->alloc_cnt == 0) { //空页交由buddy system收回
      remove_page_from_list(p_hdr);
      memset(p_hdr, 0, sizeof(struct PAGE_HDR));
      coalesce((struct BLK_HDR *)p_hdr);
    } else if (p_hdr->alloc_cnt == p_hdr->max_unit - 1) { //满页变为不满，加入free page list
      int index = get_page_list_index(p_hdr->size);
      p_hdr->blk_hdr.nxt = (struct BLK_HDR *)PAGE_LIST_HEAD(index);
      PAGE_LIST_HEAD(index) = (void *)p_hdr;
    }
  }
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
#ifdef DEBUG
  printf("%d times of realloc, ptr=%p, size=%d\n", ++realloc_cnt, ptr, size);
#endif

  if (ptr == NULL) { return mm_malloc(size); }

  void *oldptr = ptr;
  void *newptr;

  newptr = mm_malloc(size);
  if (newptr == NULL) return NULL;

  size_t copySize = get_malloc_size(oldptr);
  if(copySize > size) copySize = size;
  memcpy(newptr, oldptr, copySize);
  mm_free(oldptr);
  return newptr;
}

