---
title: "Lab3 Page Tables"
date: 2023-01-13
categories: "6.S081 OS Labs"
tags: 6.S081
---

## RISC-V Assembly

> 主要是回答一下关于汇编的问题，难度不大

## Backtrace

实现一个函数，能够打印出栈上函数调用链，以帮助出错时的调试。

原理也比较简单：利用栈结构的性质（返回地址和上一个栈帧指针在栈中存放位置是固定的），由当前栈帧指针开始，不断向上得到栈中返回地址，直到到达栈的底部。

![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230103182903462.png)

```c
void
backtrace(void){
  printf("backtrace:\n");
  // get the current frame pointer
  uint64 fp = r_fp();
  // attention: stack grow from high address to low address
  uint64 stack_bottom = PGROUNDUP(fp); // note the bottom of stack
  while (1) {
    uint64 ret_addr = *((uint64 *)(fp - 8)); // get the return address
    printf("%p\n", ret_addr);
    fp = *((uint64 *)(fp - 16)); // get the previous frame pointer
    if (PGROUNDUP(fp) != stack_bottom) break; // judge the fp cross over the stack_bottom or not
  }
}
```

## Alarm

> 要求实现一个机制，在调用了`sigalarm(interval, handler)`后，该进程每消耗`interval`个时间片，就要调用一次`handler`函数。

### 思路

实现的核心是：编写`sigalarm`、`sigreturn`两个**系统调用**和修改`usertrap`中处理**时钟中断**的部分代码。

下图是核心函数之间的调用关系，也是该部分的重点和难点，因为涉及内核空间和用户空间的频繁切换。

![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230125072846825.png)

重点需要理解的有两点：

1. `usertrap`位于内核空间，而`handler function`位于用户空间，`3`号箭头应该如何发生？

   <font color='red'>**由于`usertrap`是在处理异常的一个环节中，最后还是会返回到用户空间中，如果不做处理返回的是源代码发生中断的位置，而该位置是由`sepc`寄存器保存的，在`usertrap`中`sepc`寄存器中的值被保存在了`p->trapframe->epc`中，所以只要将`p->trapframe->epc`的值设置成`handler function`的地址即可**</font>

2. `sigreturn`系统调用是`handler function`调用的，如何使其返回时回到`user code`原先被时钟中断的那部分代码，即`5`号箭头如何发生？

   **<font color='red'>这一部分其实和第一点类似，只要设置`p->trapframe->epc`就可以使其返回到原先被中断处，但是还需要考虑如何恢复源代码的上下文，就需要恢复所有的寄存器。所以需要在`usertrap`中保存`p->trapframe`中的所有值，然后在`sys_sigreturn`处恢复</font>**

### 实现

两个系统调用

```c
// return from the handler function to user code which interrupted by time interrupt
int 
sys_sigreturn(void)
{
  struct proc* p = myproc();
  memmove(p->trapframe, p->savedtrapframe, sizeof(struct trapframe)); // restore all the registers
  p->inhandler = 0;
  p->ticksincelast = 0;
  return 0;
}

int
sys_sigalarm(void){
  int ticks; uint64 fn_addr;
  if (argint(0, &ticks) < 0 || argaddr(1, &fn_addr) < 0)
    return -1;
  struct proc* p = myproc();
  if (ticks == 0 && fn_addr == 0) { // stop generating periodic alarm calls. 
    p->ticks = -1;
    p->handler_p = 0;
  } else {
    p->ticks = ticks;
    p->handler_p = fn_addr;
  }
  p->ticksincelast = 0;
  return 0;
}
```

`usertrap`中针对时钟中断的的实现

```c
if(which_dev == 2){
    if (ticks >= 0 && !p->inhandler){
        p->ticksincelast ++;
        // kernel cause the user process to call the handler function
        // current is in kernelspace, while the handler function address is in userspace
        // so just set the p->trapframe->epc to the address of the handler function
        // when return to userspace, will call the handler function 
        if (p->ticksincelast == p->ticks) {
            memmove(p->savedtrapframe, p->trapframe, sizeof(struct trapframe));
            p->trapframe->epc = p->handler_p;
            p->inhandler = 1;
        }
    }
    yield();
}
```

### 问题

1、`usertrap`中只保存了handler函数处理完的返回地址，并没有保存一整套寄存器，导致回到`user code`之后，上下文改变了，所以发生了异常

![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230125055351113.png)

2、`p->savedtrapframe`在初始化时没有分配内存，导致发生了内存`store`时的缺页异常。

![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230125055254773.png)

3、`p->savedtrapframe`没有释放

![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230125063126826.png)
