---
title: "Lab5 Copy-on-Write"
date: 2023-01-31
categories: "6.S081 OS Labs"
tags: 6.S081
---

## COW机制介绍

COW的机制其实非常简单，核心思想还是将内存的分配推迟：在`fork()`的时候只是将子进程的虚拟页映射到父进程的物理内存上；当之后发生了实际的写内存操作引发`page fault`后，再进行实际的内存分配。

## COW实现

> 尽管COW的机制非常简单，实现起来也看似简单，但是非常易错，且调试难度也比较大（多核并发）
>
> 具体代码参考见：[cow lab](https://github.com/zhc-njdx/6.S081-Labs/tree/cow)

实现过程基本上参考手册的那几步就行：

1、修改 `uvmcopy()` 函数，将`parent`的物理页映射到`child`的页表中，而不是分配新的物理页，并且清空`PTE_W`标志，设置`cow page`的标识。

```c
// uvmcopy for cow fork
// it maps the parent's physical pages into the child
// instead of allocating the new pages
// and clear the PTE_W flag and set the cow page sign
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);

    flags = (flags & (~PTE_W)) | PTE_COW; // clear the PTE_W and set the cow sign
    *pte = PA2PTE(pa) | flags; // update the parent's pte flags

    if(mappages(new, i, PGSIZE, pa, flags) != 0){
      panic("uvmcopy: can't map the parent's physical pages into child.");
      goto err;
    }
    add_refcount(PGROUNDDOWN(pa));
  }
  return 0;

  err:
    uvmunmap(new, 0, i / PGSIZE, 0);
    return -1;
}
```

2、在`usertrap()`中识别`page fault`，并进行相应的处理。

![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230131231225955.png)

```c
...
else if (scause == 15 || scause == 12){
    // page fault
    uint64 va = r_stval(); // error va
    if (cowcopy(p->pagetable, va) != 0) {
      p->killed = 1;
    }
} 
...
```

**3、对物理页设置引用计数机制**

> 这一部分应该是最难的一块，需要考虑并发加锁。

```c
struct {
  struct spinlock lock;
  struct run *freelist;
  uint8 page_references[(PHYSTOP-KERNBASE)/PGSIZE];
} kmem;
```

4、修改`copyout`，机制和处理`page fault`一样

```c
	...
	va0 = PGROUNDDOWN(dstva);

    if (cowcopy(pagetable, va0) != 0) return -1;

    pa0 = walkaddr(pagetable, va0);
	...
```

## 错误汇总

1、报错 remap

![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230201113935903.png)

> 原因是在建立新的映射之前，要先将原来的映射取消掉；

**2、scause = 2 非法指令**

![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230201124453515.png)

> 思考排错的过程：
>
> 初步猜测是：在用户空间使用内核指令？
>
> 根据backtrace出错的位置：在用户空间是 `cowtest` 中的 `simpletest`，在`wait(0)`后面的`print`语句引发了错误；在内核空间是 `r_scause()` 引发了错误
>
> ![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230202162913328.png)
>
> ![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230202163324569.png)
>
> **可能是地址空间出现错误！**需要严格检查地址是否越界等！
>
> 但是 该错误一直没有得到解决

<font color='red'>经过长时间的debug，发现出现错误的原因是没有正确地实现引用计数机制，出现了并发的错误。</font>本来在实现的过程中，是想实现一点测试一点的，以为实现好了基本的功能应该能跑过简单测试，所以在排错的时候一直没有考虑是引用计数的问题！

**3、减少引用计数的位置**

在修改引用计数机制的时候，我是希望将引用的更新作为一个函数，然后当更新后的引用为`0`的时候再调用`kfree`释放物理内存，这个时候就会出现一个问题：这样实现相当于在`kfree`的前面加了一层引用更新函数，在`cow page`的相关实现中是可以正确调度内存的释放，但是系统的其他地方会有直接调用kfree的情况，就会导致 即使该内存页的引用不为0 该内存页还是被释放了的情况，所以减少引用计数的位置应该在`kfree`中。

## 结果

![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230210104747576.png)