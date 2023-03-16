## 1 背景要求

- 使用一个网络设备E1000来处理网络通信。
- 在模拟的LAN下，xv6的IP地址为`10.0.2.15`，主机的IP地址为`10.0.2.2`
- 当xv6使用E1000发生一个报文到`10.0.2.2`，qemu会将该报文传递给真实主机的正确应用
- 要使用QEMU的[`user-mode network stack`](https://wiki.qemu.org/Documentation/Networking#User_Networking_.28SLIRP.29)
- 所有的来往packets都记录在`packets.pcap`文件中
- `kernel/e1000.c`包含了初始化代码以及发送和接收packets的空函数
- `kernel/e1000_dev.h`包含了[寄存器和标志位的定义](https://pdos.csail.mit.edu/6.828/2021/readings/8254x_GBe_SDM.pdf)
- `kernel/net.c`和`kernel/net.h`包含了一个简单的网络栈，实现了IP、UDP、ARP协议；还包含了一个存放packet的数据结构`mbuf`
- `kernel/pci.c`包含了在xv6启动时在PCI总线上搜索E1000网卡的代码
- 实现`e1000_transmit()`和`e1000_recv()`，使得驱动程序可以传输和接收packet

- `e1000_init()`函数配置E1000从RAM读取packet并将接收到的packet写进RAM（DMA方式）
- 由于packets突发到达的速度可能比驱动程序处理他们的速度更快，`e1000_init()`提供了多个buffer给E1000去写packets
- E1000要求这些buffers由RAM中的描述符数组来描述，每个描述符包含了RAM的一个地址，E1000可以在其中写入接收到的packet
- `struct rx_desc`描述了描述符的格式，描述符的数组被称为接收循环队列
- `e1000_init()`使用`mbufalloc()`将E1000的`mbuf`数据包缓冲区分配到DMA中
- 还有一个传输循环队列用来存放需要发送的packet
- 这个两个循环队列的大小为`RX_RING_SIZE`和`TX_RING_SIZE`

- 当`net.c`中的网络栈需要发送一个packet时，会调用`e1000_transmit()`带有一个存放了需要发送的packet的`mbuf`。

完成 `e1000_transmit`：

- TDT寄存器：存放了传输描述符循环缓冲区的末尾指针，是一个偏移量。

## 2 实现

![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230316235026877.png)

![Transmit Descriptor Ring Structure](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230316235343584.png)

- 白色部分是由硬件管理的；灰色部分是由软件管理的，表示的是已经发送还未被回收的packet

```c
int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //

  // 1.acquire the lock
  acquire(&e1000_lock);
  // 2.reading the E1000_TDT control register => get the tail index of the tx ring
  uint32 tx_desc_tail_idx = regs[E1000_TDT];
  // 3.check if the ring overflowing...
  if ((tx_ring[tx_desc_tail_idx].status & E1000_TXD_STAT_DD) == 0) {
    release(&e1000_lock);
    return -1; // hasn't finish last transmition
  }
  // 4. free the mbuf if needed
  if (tx_mbufs[tx_desc_tail_idx]) {
    mbuffree(tx_mbufs[tx_desc_tail_idx]);
  }
  // 5. fill the descriptor
  tx_mbufs[tx_desc_tail_idx] = m;
  tx_ring[tx_desc_tail_idx].length = m->len;
  tx_ring[tx_desc_tail_idx].addr = (uint64)m->head;
  tx_ring[tx_desc_tail_idx].cmd = (E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS); // set the cmd flags
  // 6. update the tail pointer
  regs[E1000_TDT] = (tx_desc_tail_idx + 1) % TX_RING_SIZE;
  // 7.release the lock
  release(&e1000_lock);
  return 0;
}
```

![](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230316235048924.png)

![Receive Descriptor Ring Structure](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230316235439080.png)

- 白色部分是由硬件管理的；灰色部分是由软件管理的，表示被硬件接收但是还未被软件识别处理的packet

```c
static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  while(1) {
    // 1. acquire the lock
    // acquire(&e1000_lock);
    // 2. get the position of the next received packet
    uint32 next_packet_idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
    // 3. check if new packet available
    if ((rx_ring[next_packet_idx].status & E1000_RXD_STAT_DD) == 0) {
      // release(&e1000_lock);
      return; // no available packet
    }
    // 4. set the length and deliver the packet to the networking stack
    rx_mbufs[next_packet_idx]->len = rx_ring[next_packet_idx].length;
    net_rx(rx_mbufs[next_packet_idx]);
    // 5. refresh the mbuf
    if ((rx_mbufs[next_packet_idx] = mbufalloc(0)) == 0)
      panic("e1000_recv");
    // 6. refresh the metadata in rx_ring
    rx_ring[next_packet_idx].addr = (uint64) rx_mbufs[next_packet_idx]->head;
    rx_ring[next_packet_idx].status &= 0;
    // 7. update the E1000_RDT
    regs[E1000_RDT] = next_packet_idx;
    // 8. release the lock
    // release(&e1000_lock);
  }
}
```

> 注意`e1000_recv`不需要获取锁，因为该函数是在处理中断时被调用的，应该已经在之前就获得过锁了。

![测试结果](https://my-pictures-repo.obs.cn-east-3.myhuaweicloud.com/my-blog-imgs/image-20230304194643726.png)

















