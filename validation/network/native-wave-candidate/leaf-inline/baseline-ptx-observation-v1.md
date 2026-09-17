# 原版 150 核素 PTX 的只读观察

2026-09-17 01:50 UTC，原版 leaf 编译的 ptxas 仍在运行。
只读查看该编译器已生成的输入，没有启动第二次编译、拷贝大文件或修改服务器文件。

- 临时输入：`/tmp/tmpxft_00049f61_00000000-6_timed_network_math.ptx`。
- 大小：12,747,428 bytes／389,752 行。
- 只读 SHA-256：`2a5e45e194c535987a03ca15aae244d7a13775c2bc85c38b9a0a486b803ac125`。
- ptxas 参数仍是 `-arch sm_90 -m64 --fmad false`。
- `JacobianSink<CsrMatrixView<151>>::set` 的定义始于 17083 行。

该函数首先读取动态 row/column，然后出现以 row=75、38、19、10、5 等为阈值的
元数据查找分支。原版 set 未内联，因此这里并没有直接使用各个 Jacobian 写入位置
已知的行／列常量。源码中的短函数不一定对应短的运行代码，值得做实际原版／内联对照。

这是一个临时中间代码观察，不是耗时归因、完整 PTX 归档、SASS 资源统计或性能通过。
PTX 的 `.reg` 声明是虚拟寄存器信息，不能把其中编号当作真实硬件寄存器占用。
等待实际 CUDA executable、kernel attributes、完整向量及 ABBA 结果后再决定是否采用。

## 01:56 UTC 的候选对照（仍未 GPU 运行）

原版150已编译／链接成功，time-v wall 18:02.38、peak RSS 1,848,740 KiB、exit0；
可执行文件 SHA `506c3ac46c61c142494b37cd6a669ed5813c7ca7db02eed762e5c1056f9e086d`。
候选仍在 ptxas，临时输入为 `/tmp/tmpxft_0004b1ba_00000000-6_timed_network_math.ptx`：
16,920,456 bytes／546,612 行，SHA `9844f86c150b967b9ffd9e1ea91f679944f5c79cbc2d8b08ff26f32a369a9a51`。

相同 grep 口径，独立 JacobianSink::set 函数定义由 1 个变为 0 个，
全 PTX 的静态 `call.uni` 出现次数由 4,532 降为 2,169；PTX 文本体积反而增加。
这确认候选编译器确实处理了不同内联布局，也提示编译成本／代码体积需要同时记录。
静态 call 出现次数不是运行次数，PTX 文本大小也不是 GPU 指令缓存占用或性能；
不从这些数字计算加速比。
