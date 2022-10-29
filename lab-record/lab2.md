# Lab2: System Call



## 1. Preview

### 1.1 xv6-book Chap2

xv6-book的第二章和lecture3的内容类似，主要介绍了操作系统的组织结构，从物理资源的抽象、用户态/内核态、系统调用、微内核/宏内核以及代码层面展开

xv6-book的4.3、4.4节讲的是如何进行系统调用

### 1.2 code

了解xv6启动过程

`_entry.S`中将`stack0+4096`赋给栈指针寄存器`sp`，使得其指向栈顶，然后`call start`

`start` => `main` => `userinit` => `initcode.S` => `init.c`



## 2 System call tracing

`trace`是一个工具，能够记录指定的系统调用。

```shell
$ trace 32 grep hello README
3: syscall read -> 1023
3: syscall read -> 966
3: syscall read -> 70
3: syscall read -> 0
```

```shell
# 命令的格式
$ trace [MASK] [OPTIONS...] # 其中[MASK]是一个数字n; 如果 (n >> i) & 1 == 1 表示i号系统调用需要trace
# 输出的形式
[pid]: syscall <name> -> <return_value>
```

首先需要明确的是`trace`也是一个系统调用，所以就需要大概明白从用户态调用`trace`工具到内核调用对应的系统调用的过程。

根据手册的指示大概能够推测出来

1、在命令行中输入：`trace 32 grep hello README` 后，实际上是执行 /user/trace.c 文件。过程就是 先执行 `trace` 函数，然后再执行后面的命令。

2、这个 trace 函数是需要在  /user/user.h 文件中定义原型的，之后好像就找不到对应的实现了。其实之后的实现是在内核态了，需要先陷入内核，手册中说要在 /user/usys.pl 中定义一个 stub: `entry("trace")`，这个stub会在 user/usys.S 生成一段汇编代码：进行系统调用。

3、其中的`ecall`指令就会调用 /kernel/syscall.c 中的 `syscall` 函数，执行对应的系统调用函数 sys_<name>

然后就可以开始根据手册的提示写代码了...

1. 在 kernel/sysproc.c 中增加 sys_trace() 函数
2. 要在 proc 结构体中增加一个新的变量存储 trace 的参数
3. 修改 syscall() 函数来打印 trace 输出
4. 修改 fork() 函数使得 trace 的参数从父进程拷贝到子进程

```c
// 在 kernel/sysproc.c 中增加 sys_trace 函数
uint64
sys_trace(void){
  int n;
  if( argint(0, &n) < 0 ){
    return -1;
  }
  // parse the `n` to get which sys_call need to be traced
  struct proc* p = myproc();
  p->trace_mask = n;
  
  return 0;
}
```

```c
// 修改 kernel/syscall.c 中的 syscall 函数
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7; // a7: sys_call number
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();
    // add the trace check
    if( (( p->trace_mask >> num ) & 1) == 1 ){ // need to trace
      printf("%d: syscall %s -> %d\n", p->pid, sys_call_names[num], p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

测试结果

> 注意第四个测试有可能会超时，需要修改 gradelib.py 文件的第 428 行 扩大时间限制

![image-20221029212319005](https://my-picture-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20221029212319005.png)

## 3 Sysinfo

还是实现一个系统调用，在内核填上 `struct sysinfo` 的两个字段，并拷贝回用户空间，主要过程：

1. 像`trace`那样，在对应位置增加系统调用所需的相关信息。
2. 在 `kernel/proc.c` 中增加一个统计 `not UNUSED process` 的函数
3. 在 `kernel/kalloc.c` 中增加一个统计 `free memory` 的函数
4. 理解 `copyout` 函数，在系统调用中将 `struct sysinfo` 从内核空间 拷贝入 用户空间

核心代码

```c
// need to copy the sysinfo struct from kernel space to user space
uint64
sys_sysinfo(void){
  uint64 addr;
  if( argaddr(0, &addr) < 0 ){
    return -1;
  }
  struct sysinfo si;
  si.nproc = notunusedproc();
  si.freemem = freemem();
  struct proc* p = myproc();
  if( copyout(p->pagetable, addr, (char *)(&(si)), sizeof(si)) < 0 ){
    return -1;
  }
  return 0;
}
```

统计 不是 UNUSED 的 进程数量，只需要遍历 proc 数组即可。

```c
uint64
notunusedproc(void){
  int num = 0;
  for(int i = 0; i < NPROC; i++){
    if(proc[i].state != UNUSED){
      num++;
    }
  }
  return num;
}
```

统计 free memory，需要读一下 `kalloc.c` 的代码，会发现 在 `kalloc` 函数中，如果 `kmem.freelist` 不为空的话就会分配一个 `PGSIZE` 的内存空间，所以只需要统计 `kmem.freelist` 链表长度即可。

```c
uint64
freemem(void){
  struct run* r = kmem.freelist;
  int num = 0;
  while(r){
    r = r->next;
    num++;
  }
  return num * PGSIZE;
}
```

测试结果

![image-20221029212340499](https://my-picture-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20221029212340499.png)
