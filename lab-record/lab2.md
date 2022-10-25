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

1、在命令行中输入：`trace 32 grep hello README` 后，实际上时执行 /user/trace.c 文件。过程就是 先执行 `trace` 函数，然后再执行后面的命令。

2、这个 trace 函数是需要在  /user/user.h 文件中定义原型的，之后好像就找不到对应的实现了。其实之后的实现是在内核态了，需要先陷入内核，手册中说要在 /user/usys.pl 中定义一个 stub: `entry("trace")`，这个stub会在 user/usys.S 生成一段汇编代码：进行系统调用。

3、其中的`ecall`指令就会调用 /kernel/syscall.c 中的 `syscall` 函数，执行对应的系统调用函数 sys_<name>

然后就可以开始写代码了...
