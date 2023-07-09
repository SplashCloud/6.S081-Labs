// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// #define LAB_LOCK

#ifndef LAB_LOCK
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;


void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    // insert into the front of the head everytime
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}
#endif

#ifdef LAB_LOCK

#define BUCKETS 13

struct bucket {
  struct spinlock lock;
  struct buf head;
};

struct {
  struct bucket buckets[BUCKETS];
  struct buf buf[NBUF];
  struct spinlock lock;
} bcache;

int
hash(uint dev, uint blockno)
{
  unsigned long long v = dev;
  v = ((v << 32) | blockno);
  return v % BUCKETS;
}

/**
 * @brief initialize the new bcache with 13 buckets
*/
void 
binit(void)
{
  initlock(&bcache.lock, "bcache");
  int idx = 0;
  for (int i = 0; i < BUCKETS; i ++) {

    initlock(&bcache.buckets[i].lock, "bcache-"+i);

    struct buf *head = &bcache.buckets[i].head;

    head->next = head;
    head->prev = head;

    int end = idx + NBUF/BUCKETS; // put 2 buf in every bucket except the last one
    if (i == BUCKETS - 1) end = NBUF;

    // printf("buckets=%d", end-idx);

    for (; idx < end; idx ++) {
      struct buf *b = &bcache.buf[idx];
      b->next = head->next;
      b->prev = head;
      head->next->prev = b;
      head->next = b;
    }

  }
}

/**
 * @brief find the least-recently used unused buffer from the other buckets and remove it from the bucket.
 * @attention this function should be invoke after acquire the lock of the bucket.
*/
struct buf *
findlrubufunlock(int idx) {
  struct buf *head = &bcache.buckets[idx].head;
  struct buf *b;
  struct buf *lrub = 0;
  for (b = head->next; b != head; b = b->next) {
    if (b->refcnt != 0) continue;
    if (lrub == 0 || (lrub->lastuse > b->lastuse)) lrub = b;
  }
  return lrub;
}

/**
 * @brief insert the buffer into the front of the head in the bucket.
 * @attention this function should be invoke after acquire the lock of the bucket.
*/
void insertbufunlock(int idx, struct buf *b) {
  struct buf *head = &bcache.buckets[idx].head;
  b->next = head->next;
  b->prev = head;
  head->next->prev = b;
  head->next = b;
}

static struct buf*
bget(uint dev, uint blockno)
{
  int idx = hash(dev, blockno);
  struct buf *b;
  acquire(&bcache.buckets[idx].lock);
  struct buf *head = &bcache.buckets[idx].head;
  
  // 1. is the block already cached
  for (b = head->next; b != head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.buckets[idx].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // need to release the lock of the buckets[idx] to avoid the deadlock
  // consider the condition:
  // thread-1 want to get a buffer from bucket#1, but it isn't cached in bucket#1
  //         and assume that it still acquires the bucket#1's lock 
  //         and continue to traverse other buckets to evict a buffer
  //         maybe it is traversing the bucket#2, so it want to acquire the bucket#2's lock
  // meanwhile thread-2 want to get a buffer from bucket#2, but also it isn't cached in bucket#2
  //         and it still acquires the bucket#2's lock
  //         and continue to traverse other buckets to evict a buffer
  //         maybe it is traversing the bucket#1, so it want to acquire ths bucket#1's lock
  // so the deadlock appear
  // thread-1 acquired bucket#1's lock and want to acquire bucket#2's lock
  // thread-2 acquired bucket#2's lock and want to acquire bucket#1's lock
  release(&bcache.buckets[idx].lock);

  acquire(&bcache.lock);

  acquire(&bcache.buckets[idx].lock);
  for (b = head->next; b != head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.buckets[idx].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.buckets[idx].lock);

  // 2. not cached
  // need to find the least recently used and unused buffer
  b = 0; // record the l-r-u buffer
  int bucket_idx = -1; // record the lock of the bucket holding the buffer
  for (int i = 0; i < BUCKETS; i ++) {
    acquire(&bcache.buckets[i].lock); // lock the bucket which has possible target buffer
    struct buf *b0 = findlrubufunlock(i);
    // if (i == BUCKETS - 1 && !b && !b0) i = -1; // loop while b is none
    if (!b0) { // no unused buffer in the bucket
      release(&bcache.buckets[i].lock);
      continue;
    }
    // prevent the previously found available buffer from being updated to become unavailable later
    // so the proccess should acquire the lock of the bucket where the target buffer in it
    // until the new target buffer appears.
    // when the new target buffer appears, should release the previous bucket's lock
    if (!b || b->lastuse > b0->lastuse) {
      b = b0; // update the bufferß
      if (bucket_idx != -1) release(&bcache.buckets[bucket_idx].lock); // release the prev lock
      bucket_idx = i; // update the lock
    } else {
      release(&bcache.buckets[i].lock); // not target buffer, release the lock
    }
  }
  if (b) { // the l-r-u unused buffer exist
    if (bucket_idx != idx) {
      // remove from the origin bucket
      b->prev->next = b->next;
      b->next->prev = b->prev;
      release(&bcache.buckets[bucket_idx].lock); // get the buffer, so could release the lock to avoid deadlock
      // insert into current bucket
      acquire(&bcache.buckets[idx].lock);
      insertbufunlock(idx, b);
    }
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    release(&bcache.buckets[idx].lock);
    release(&bcache.lock);
    acquiresleep(&b->lock);
    return b;
  }
  // no unused buffers
  panic("bget: no buffers");
}

struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

void
brelse(struct buf *b) 
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int idx = hash(b->dev, b->blockno);
  acquire(&bcache.buckets[idx].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->lastuse = ticks;
  }
  release(&bcache.buckets[idx].lock);
}

void
bpin(struct buf *b)
{
  int idx = hash(b->dev, b->blockno);
  acquire(&bcache.buckets[idx].lock);
  b->refcnt++;
  release(&bcache.buckets[idx].lock);
}

void
bunpin(struct buf *b)
{
  int idx = hash(b->dev, b->blockno);
  acquire(&bcache.buckets[idx].lock);
  b->refcnt--;
  release(&bcache.buckets[idx].lock);
}

#endif