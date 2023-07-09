// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};
#ifndef LAB_LOCK
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
#endif

#ifdef LAB_LOCK
struct mem {
  struct spinlock lock;
  struct run *freelist;
};

struct mem mems[NCPU]; // define NCPU mem for each CPU

void
kinit()
{
  for (int i = 0; i < NCPU; i ++) {
    initlock(&mems[i].lock, "kmem-"+i);
  }
  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa%PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP) 
    panic("kfree");

  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  push_off();
  int id = cpuid();
  pop_off();

  acquire(&mems[id].lock);
  r->next = mems[id].freelist;
  mems[id].freelist = r;
  release(&mems[id].lock);
}

/**
 * steal the mem from other cpu's freelist
 * when a cpu's freelist was run out of.
 * @param id the id of the cpu which want to steal the mem from.
*/
struct run *
stealmem(int id) {
  struct run *r;

  acquire(&mems[id].lock);
  r = mems[id].freelist;
  if (r) 
    mems[id].freelist = r->next;
  release(&mems[id].lock);

  return r;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();
  pop_off();

  acquire(&mems[id].lock);
  r = mems[id].freelist;
  if (r)
    mems[id].freelist = r->next;
  release(&mems[id].lock);

  if (!r) { // steal the mem from another cpu
    for (int i = 0; i < NCPU; i++) {
      if (i == id) continue;
      r = stealmem(i);
      if (r) break;
    }
  }

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk data

  return (void *)r;
}
#endif