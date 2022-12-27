# Lab page tables



## 0 lecture 4 & chapter 3

### Lecture 4

### Chapter 3 Page tables

#### xv6的页表映射机制

![](https://my-picture-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20221225144537100.png)

#### xv6 内核地址空间

![](https://my-picture-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20221225144757600.png)

#### xv6的页表代码

> `vm.c`

核心数据结构：==pagetable_t==

核心函数：==walk== 和 ==mappages==

#### 物理地址分配代码

> `kalloc.c`

核心数据结构：==kmem==

核心函数：==kfree== 和 ==kalloc==

#### 进程地址空间

![](https://my-picture-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20221225152730759.png)

#### 系统调用：sbrk 和 exec

**sbrk**：为一个进程去减少或者增加它的内存（`kernel/sysproc.c`）

**exec**：创建一个地址空间的用户部分

> <font color='red'>what is trampoline?</font>
>
> trampoline page存储了用户空间和内核空间相互切换的代码，无论是在内核空间还是在用户空间它都映射在相同的虚拟地址，这样在切换之后还可以继续工作。
>
> 相关文章：[What is trampoline?](https://xiayingp.gitbook.io/build_a_os/traps-and-interrupts/untitled-3)
>
> <font color='red'>what is trapframe?</font>
>
> `trapframe`是存在于用户地址空间，位于`trampoline`下面的大小为`PGSIZE`（4096字节）的一块内存，用于在用户地址空间向内核地址空间切换时保存用户空间的寄存器。

## 1 Speed up system calls

<u>任务描述</u>：加速`getpid()`系统调用。方法是在`trapframe`前面映射一个只读的页，在这个页的开始，存储一个结构体`syscall`，结构体里存储当前进程的`pid`，然后通过已经提供的`ugetpid()`函数获得`pid`。

<u>思路</u>：可以参考`trapframe`的构造。

```c
// 步骤
// 1. 在proc结构体中增加一个usyscall字段
// 2. 在allocproc()函数为usyscall分配空间，并且将pid存储在usyscall中
// 3. 在proc_pagetable()函数中将p->usyscall（物理地址）映射到USYSCALL（虚拟地址）
// 4. 在freeproc()函数中将usyscall的空间释放
// 5. 在proc_freepagetable()函数中取消之前建立的映射
```

## 2 Print a page table

<u>任务描述</u>：如题要求打印页表。

<u>思路</u>：参考`freewalk`函数进行递归。

```c
int level = 1;
void
vmprint(pagetable_t pagetable){
    if(level > 3) return;
    
    if(level == 1)
        printf("page table %p\n", pagetable);
    
    for(int i = 0; i < 512; i++){
        pte_t pte = pagetable[i];
        if(pte & PTE_V){
            for(int j = 0; j < level; j++){
                printf("..");
                if(j != level - 1) printf(" ");
            }
            uint64 child = PTE2PA(pte);
            printf("%d: pte %p pa %p\n", i, pte, child);
            level++;
            vmprint((pagetable_t)child);
            level--;
        }
    }
}
```

## 3 Detecting which pages have been accessed

<u>任务描述</u>：检测页表是否被访问，实现`pgaccess`系统调用。

<u>思路</u>：通过`walk`函数找到虚拟地址对应的`pte`，检查`PTE_A`位即可。

```c
// sysproc.c
int
sys_pgaccess(void){
    uint64 base;
    int len;
    uint64 mask;
    if(argaddr(0, &base) < 0 || argint(1, &len) < 0 || argaddr(2, &mask) < 0)
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
```

> :zap:访问页表时将`PTE_A`置1的工作由RISC-V硬件做了，在代码中不需要自己设置。
