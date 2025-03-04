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
#define NBUCKT 13
#define hash(x) (x)%NBUCKT
// int
// hash(int blockno)
// {
//   return blockno % NBUCKT;
// }
struct {
  struct spinlock lock[NBUCKT];
  struct buf buf[NBUF];
  struct spinlock biglock;
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKT];
} bcache;
void
binit(void)
{
  struct buf *b;
  for(int i=0;i<NBUCKT;i++){
  initlock(&bcache.lock[i], "bcache");
  }

  // Create linked list of buffers
  for(int i=0;i<NBUCKT;i++){
  bcache.head[i].prev = &bcache.head[i];
  bcache.head[i].next = &bcache.head[i];
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head[0].next;
    b->prev = &bcache.head[0];
    initsleeplock(&b->lock, "buffer");
    bcache.head[0].next->prev = b;
    bcache.head[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct buf *b2=0;
  int minsize=0;
  uint64 id=hash(blockno);
  acquire(&bcache.lock[id]);

  // Is the block already cached?
  for(b = bcache.head[id].next; b != &bcache.head[id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.lock[id]);
  acquire(&bcache.biglock);
  acquire(&bcache.lock[id]);
    for (b = bcache.head[id].next; b != &bcache.head[id]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.lock[id]);
      release(&bcache.biglock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head[id].next; b != &bcache.head[id]; b = b->next){
    if(b->refcnt == 0&&(b->lastuser<minsize||b2==0)) {
      minsize=b->lastuser;
      b2=b;
    }
  }
  if(b2){
      b2->dev = dev;
      b2->blockno = blockno;
      b2->valid = 0;
      b2->refcnt = 1;
      // b2->lastuser=ticks;
      release(&bcache.lock[id]);
      release(&bcache.biglock);
      acquiresleep(&b2->lock);
    return b2;
  }
  for(int i=hash(id+1);i!=id;i=(hash(i+1)))
  {
    acquire(&bcache.lock[i]);
    for(b = bcache.head[i].next; b != &bcache.head[i]; b = b->next){
    if(b->refcnt == 0&&(b->lastuser<minsize||b2==0)) {
      minsize=b->lastuser;
      b2=b;
    }
  }
  if(b2)
  {
    b2->dev = dev;
    b2->blockno = blockno;
    b2->valid = 0;
    b2->refcnt = 1;
    // b2->lastuser=ticks;
    //  b2->next->prev = b2->prev;
    //   b2->prev->next = b2->next;
    //   b2->next = bcache.head[id].next;
    //   b2->prev = &bcache.head[id];
    //   bcache.head[id].next->prev = b2;
    //   bcache.head[id].next = b2;
    //   release(&bcache.lock[i]);
    //   release(&bcache.lock[id]);
    //   release(&bcache.biglock);
    //   acquiresleep(&b2->lock);
    b2->next->prev = b2->prev;
      b2->prev->next = b2->next;
      release(&bcache.lock[i]);
      // add block
      b2->next = bcache.head[id].next;
      b2->prev = &bcache.head[id];
      bcache.head[id].next->prev = b2;
      bcache.head[id].next = b2;
      release(&bcache.lock[id]);
      release(&bcache.biglock);
      acquiresleep(&b2->lock);
    return b2;
  }
  release(&bcache.lock[i]);
  }
   release(&bcache.lock[id]);
   release(&bcache.biglock);
  panic("bget: no buffers");
}

// static struct buf*
// bget(uint dev, uint blockno)
// {
//   struct buf *b, *b2 = 0;

//   int i = hash(blockno), min_ticks = 0;
//   acquire(&bcache.lock[i]);

//   // 1. Is the block already cached?
//   for(b = bcache.head[i].next; b != &bcache.head[i]; b = b->next){
//     if(b->dev == dev && b->blockno == blockno){
//       b->refcnt++;
//       release(&bcache.lock[i]);
//       acquiresleep(&b->lock);
//       return b;
//     }
//   }
//   release(&bcache.lock[i]);

//   // 2. Not cached.
//   acquire(&bcache.biglock);
//   acquire(&bcache.lock[i]);
//   // 2.1 find from current bucket.
//   for (b = bcache.head[i].next; b != &bcache.head[i]; b = b->next) {
//     if(b->dev == dev && b->blockno == blockno) {
//       b->refcnt++;
//       release(&bcache.lock[i]);
//       release(&bcache.biglock);
//       acquiresleep(&b->lock);
//       return b;
//     }
//   }
//   // 2.2 find a LRU block from current bucket.
//   for (b = bcache.head[i].next; b != &bcache.head[i]; b = b->next) {
//     if (b->refcnt == 0 && (b2 == 0 || b->lastuser < min_ticks)) {
//       min_ticks = b->lastuser;
//       b2 = b;
//     }
//   }
//   if (b2) {
//     b2->dev = dev;
//     b2->blockno = blockno;
//     b2->refcnt++;
//     b2->valid = 0;
//     //acquiresleep(&b2->lock);
//     release(&bcache.lock[i]);
//     release(&bcache.biglock);
//     acquiresleep(&b2->lock);
//     return b2;
//   }
//   // 2.3 find block from the other buckets.
//   for (int j = hash(i + 1); j != i; j = hash(j + 1)) {
//     acquire(&bcache.lock[j]);
//     for (b = bcache.head[j].next; b != &bcache.head[j]; b = b->next) {
//       if (b->refcnt == 0 && (b2 == 0 || b->lastuser < min_ticks)) {
//         min_ticks = b->lastuser;
//         b2 = b;
//       }
//     }
//     if(b2) {
//       b2->dev = dev;
//       b2->refcnt++;
//       b2->valid = 0;
//       b2->blockno = blockno;
//       // remove block from its original bucket.
//       b2->next->prev = b2->prev;
//       b2->prev->next = b2->next;
//       release(&bcache.lock[j]);
//       // add block
//       b2->next = bcache.head[i].next;
//       b2->prev = &bcache.head[i];
//       bcache.head[i].next->prev = b2;
//       bcache.head[i].next = b2;
//       release(&bcache.lock[i]);
//       release(&bcache.biglock);
//       acquiresleep(&b2->lock);
//       return b2;
//     }
//     release(&bcache.lock[j]);
//   }
//   release(&bcache.lock[i]);
//   release(&bcache.biglock);
//   panic("bget: no buffers");
// }

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
  // printf("%d\n",b->dev);
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
  int id=hash(b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head[id].next;
    // b->prev = &bcache.head[id];
    // bcache.head[id].next->prev = b;
    // bcache.head[id].next = b;
    b->lastuser=ticks;
  }
  
  release(&bcache.lock[id]);
}


void
bpin(struct buf *b) {
  int id=hash(b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt++;
  release(&bcache.lock[id]);
}

void
bunpin(struct buf *b) {
  int id=hash(b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt--;
  release(&bcache.lock[id]);
}


