#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

#ifdef LAB_PGTBL
// int pgaccess(void *base, int len, void *mask);
int
sys_pgaccess(void)
{
  // lab pgtbl: your code here.
  uint64 base; // starting virtual address of the first user page to check.
  int len; // the number of pages to check.
  uint64 mask; // user address to a buffer to store the results into a bitmask
  if(argaddr(0, &base) < 0 || argint(1, &len) < 0 || argaddr(2, &mask) < 0 )
    return -1;
  
  uint64 start = PGROUNDDOWN(base);
  uint64 bitmask = 0L;
  for(int i = 0; i < len; i++, start += PGSIZE){
    pte_t *pte = walk(myproc()->pagetable, start, 0);
    if(pte == 0) return -1; // page not map
    uint64 flag = (*pte & PTE_A) >> 6;
    if(flag){
      *pte ^= PTE_A; // clear the PTE_A
    }
    bitmask |= (flag << i);
  }
  if(copyout(myproc()->pagetable, mask, (char *)&bitmask, sizeof(uint64)) < 0)
    return -1;
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
