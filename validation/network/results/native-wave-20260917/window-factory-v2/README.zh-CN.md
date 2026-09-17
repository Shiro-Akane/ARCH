# 窗口 factory：全新构建通过，真实轨迹尚未运行

2026-09-17。原网络、共享数学、strict-FP、sm90、原依赖库及已通过合同的 native provider 保持不变。
没有叠加 leaf-inline，没有借用旧 CUDA 对象，没有编译完整 ARCH 或启用生产注册。

## 实际构建结果

完整 11 条命令均 exit 0：Host wrapper；每个网络的依赖预扫描、新 CUDA 对象、
新 Host harness 对象、链接、ldd。两个 NVCC 实际依赖清单均绑定新 owner/executor/window
头，未引用旧 source 头。原 474 文件完整副本只有已审阅两执行头改变，另加两窗口文件。

| 新 CUDA factory | 编译 wall | peak RSS（KiB） | swaps |
|---|---:|---:|---:|
| audit150 | 49:42.91 | 2,714,276 | 0 |
| audit200 | 1:47:00 | 4,004,036 | 0 |

七个新对象/可执行文件、实际编译/链接命令、全部私有源库存与依赖身份见
[原始构建记录](records/ARCH-native-wave-v4-20260916/window-factory-v1/record.json)。

新可执行文件 SHA-256：

- audit150：`0b2ba61e835d050db3ab057a36cb8dc37ee2f70c76e0b5a61f7f25a8b61810a5`
- audit200：`584d82d8aeaef8df61e292f68cbb8a3e9ad27636ad588c3a0bdf150f18e173dc`

原 guard 完整结束，wall 9,434.165 s，owned peak RSS 4,035,412 KiB；
Host minimum available 109,067,700 KiB，swap 9,256 KiB 前后不增，GPU used peak 0 MiB。
系统 memory-full/IO-full 瞬时峰值分别 27.456%/48.896%，未满足持续中止条件；
`guard_stopped=False`，不能将其描述为全程零压力。

## 归档恢复和双端核验

首次 collector 因 SSH 登录 PATH 没有 Ninja 失败；改为从已核验的原 CMakeCache
读取绝对工具路径。之后的 v1 归档又因编译器 `../` 路径别名造成重复 inode/hard-link
成员，被本机严格校验器拒绝。两次都是收集工程问题，没有重编或重跑科学测试。

新 collector 保留实际依赖字符串及哈希，但只归档去重后的规范文件路径，另写 v2；
严格校验器没有修改。失败的 v1 collector、归档和说明全部保留：失败归档在新 raw 中
仅作为不透明证据文件，不作为合格 source tree，不应再嵌套解包为测试输入。

09:51:06 UTC，本机与服务器的两个新归档及全部成员字节核验完成：

- raw：84,210,446 bytes；SHA `721fba204dca11336eb18a65f19718e09ae1d924ffc05506f906b3c18d082cca`。
- compact：139,904 bytes；SHA `e19915ea690a6506ca90c8de9d2e3cc5c7a29059ed2b6b61daf4705669d45090`。
- 原始清单 560 文件、143,032,799 bytes；Git 投影 78 文件、raw-only 482 文件。
- [服务器收集回执](window-factory-collection-v2.json)、[本机严格核验回执](window-factory-local-receipt-v2.json)。
- 本机 `build/window-factory-v2-download` 和 `build/window-factory-v2-verified` 保留原始归档/提取数据；服务器副本也保留。

## 下一步与当前阻塞

仅完成 build gate。原 150/200 × BE_NR/BD/ROS4、2→3 单元、pool2、四步 1e-10 的
六组 focused 输入已准备为新包 v2，只更新归档前置版本；原科学 validator 与预算不变。
旧输入 v1 从未上传/运行，仍保留。新输入也尚未上传/dispatch。

09:54–09:55 UTC 启动前检查发现另一个项目的 ComfyUI Python 进程 PID 610350
占用约 449 MiB GPU，独占检查拒绝启动。没有停止对方进程，也没有绕过空闲检查。
需释放 GPU 后继续六组真实回归。此记录不构成 ODE、多页窗口、Helm 全应用、
sanitizer 或性能通过；大网络的性能目标仍未验收。
