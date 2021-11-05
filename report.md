首先看一下case中最大的单次分配是多少，发现除了一个corner case，最大不会超过1MB，于是按照最大分配<1MB进行设计。

```shell
$> find ./traces -name "*.rep" | xargs cat | awk '{print $3}' | sed '/^$/d' | sort -nr | head -n 10
52428800
614784
614784
614656
614656
614528
614528
614400
614400
614272
```

使用了两级的分配方式：

1. 上层是buddy system，每一级称为一个block。对足够大的malloc请求会放在对应order的block里。如果当前order没有空闲block，需要从更上级的block进行拆分，直至使用`sbrk`扩展堆大小并作为最高order的block。如果一个block和其buddy都空闲，就会将其合并为高一order的block。

2. 下层是slab allocator，page是从buddy system获取的，order为0的block。所有请求被上浮到某些固定的分配大小，每个page存放某种固定的分配大小，每种大小有一个free page list。page内部采用header+data的方式存储，header里包含一张bitmap标记页内位置是否空闲。

堆的最前面是一个HEAP_HDR，存放各种order block的free list，以及各种大小page的free list。



测试样例感觉有些问题：

1. `f -1`不知道是什么意思。
2. driver在连续运行多个样例时会复用同样的内存地址作为heap，导致上一轮遗留的数据结构被错误使用。理论上这个问题需要我来解决，暂时使用了在`mem_reset_brk`里用`memset`将堆清零的方式。
3. 默认的heap大小有点不太够，稍微扩大了一些。
4. 对于每个样例，driver运行correctness，efficiency，performance三轮。上一轮留下的操作计数会遗留到下一轮。（比如对于realloc，`r 0 16`在第一轮参数是空指针，第二轮就不再是了）

