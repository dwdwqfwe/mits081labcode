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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];
struct
{
  struct spinlock lock;
  struct run *freelist;
}kmem_cpu[NCPU];
void
kinit()
{
  for(int i=0;i<NCPU;i++){
  initlock(&kmem[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  // uint64 n=cpuid();
  // printf("%d\n",n);
  // uint64 nsize=(uint64)(pa_end-pa_start)/NCPU;
  p = (char*)(PGROUNDUP((uint64)pa_start));
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
  uint64 n=cpuid();
  r = (struct run*)pa;

  acquire(&kmem[n].lock);
  r->next = kmem[n].freelist;
  kmem[n].freelist = r;
  release(&kmem[n].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int n=cpuid();
  acquire(&kmem[n].lock);
  r = kmem[n].freelist;
  if(!r){
        for(int i=0;i<NCPU;i++)
    {
      if(i==n){
      continue;
      }

      if(kmem[i].freelist)
      {
        kmem[n].freelist=kmem[i].freelist;
        r=kmem[n].freelist;
        kmem[i].freelist=(struct run*)(0);
        break;
      }
    }
  }
  if(r){
    kmem[n].freelist = r->next;
  }
  release(&kmem[n].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
