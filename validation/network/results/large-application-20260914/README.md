# 150／200 核素完整应用构建与验证

当前完成的是 **clean Release 完整构建和链接**；数值运行、修复整合及正式性能仍待本目录后续记录。
不是 focused harness 的通过代替完整 ARCH 验收。

## 原始 clean 构建

生产源码为 `32cc416220a69cad5ab12030ba7b26db360bcd22`，工作树位于服务器
`/home/ubuntu/projects/ARCH-large-application-20260914`。
两个新计时脚本／测试是未提交文件，完整 provenance 已记录；生产源码没有未提交修改。
严格浮点、既有 SuiteSparse KLU 和 cuDSS／cuBLAS 库保持不变，目标 sm90。

八个 EOS／网络组合均成功生成，没有跳过失败 TU：

| 网络 | EOS | 编译墙钟（秒） | 单命令 peak RSS（KiB） |
|---|---|---:|---:|
| 150 | IdealGas | 3368.02 | 2714144 |
| 150 | Helm | 3438.08 | 2823740 |
| 150 | Tabular3D | 5875.71 | 3020496 |
| 150 | Tabular4D | 5067.86 | 3184264 |
| 200 | IdealGas | 7015.43 | 4004052 |
| 200 | Helm | 7429.00 | 4240728 |
| 200 | Tabular3D | 10800.80 | 4424572 |
| 200 | Tabular4D | 11478.57 | 4536864 |

共记录 116 次 CXX／CUDA 编译调用；SuiteSparse 的 C 编译未经过逐命令 launcher。
backend archive、sparse provider archive、完整 ARCH 和 checkpoint validator 都已生成，指纹见
`clean/artifacts.sha256`，完整命令、首轮配置失败和成功配置日志亦保留。
采用服务器并行 4 构建并有其他工作负载；此表不是编译加速比，也不是 16 GiB Debug 资格。
内置网络的 16 GiB 受限 Debug 验证另见 composition-fix 结果，不外推到超大网络。

## 备份和后续边界

`clean/summary.json` 绑定原始源码、生成网络、依赖库、build options 与产物。
compact 构建日志包已在服务器和本机分别核验：

- `large-clean-build-evidence-v1.tar.zst`：244,127 bytes。
- SHA-256：`c92ba070a934bbddea27ba6bb45c071f20bff511dda5fb41dab368e1c50cba09`。
- 服务器上述工作树的 `build/`；本机 `C:/tmp/ARCH-perf-20260909/build/`。

该包只包含构建证据，不包含二进制。后续整合先保存四个原产物，再由 Ninja 按真实依赖重编。
整合将明确标为“原 clean 构建 + 已记录的增量整合”，不冒称第二次 clean 构建。
完整应用运行尚未通过；原 focused BE 长轨迹大容量超时继续保留，不以缩小问题或更换 ODE 覆盖。
