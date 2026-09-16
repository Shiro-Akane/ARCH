# native-wave v4：八组独立 GPU 合同通过

2026-09-17（北京时间），H100-20C vGPU、driver 570.133.20、NVCC 12.8.93、G++ 11.4、cuDSS 0.8.0.10。原正式矩阵及 BE 长程证据结束、双端归档并审阅后才启动；没有与原采样并发。

新编译 provider、共享 equilibration CUDA TU、测试 TU，并重新链接隔离 executable。三个对象与 executable 的 SHA 都在 `records/ARCH-native-wave-v4-20260916/contracts/record.json`；原 helper archive 只提供原结果处理接口，旧对象不冒充新 provider。实际命令保留 strict FP、sm90、GPU-only、BTF_COLAMD、IR=2；无库替换、Host factor spill 或 CPU fallback。

151／201 阶 × 1／2／8／32 系统共八组均有唯一 `SPARSE_WAVE_CONTRACT_PASS`，共 17 个环境／编译／链接／运行命令 exit 0。测试包括已知解与原系统残差、量纲缩放、尾部／inactive、不改输入、factor/solve/reuse、token／地址失配拒绝、失效重建以及 NaN／奇异系统的失败与恢复。任意运行异常或资源失败不被算作负向合同“通过”。

资源护栏完整结束：wall 16.451 秒，所属进程峰值 RSS 317792 KiB，最小 Host available 114223020 KiB，swap 9256 KiB 不变；whole-device 显存峰值 569 MiB，最低空闲 19195 MiB。测试矩阵是稀疏三对角制造解，不是核反应 Jacobian；此显存／耗时不能外推真实网络。两份原始包和全部成员／文本投影字节已本机复核。

- raw：`native-wave-standalone-v4-raw.tar.zst`（服务器名 `standalone-raw-v1.tar.zst`），1148716 bytes，SHA `c8800b600c7e745c3d61a16889b8791c200b754a3e809a507ef562a6e7dc6fc3`。
- compact：208340 bytes，SHA `1d469ca0d8d043bfc8aa1712cfc92fded16ffb61d84eb42a982176e57e43fc24`。
- raw 中 70 文件、5697344 bytes；Git 文本投影 64 文件。11 个外部依赖保留绝对路径、真实路径、大小和 SHA，不重新复制／修改系统及第三方库；见 `external-dependencies.json`。
- 服务器根 `/home/ubuntu/projects/ARCH-native-wave-v4-20260916/`；本机包在 `C:/tmp/ARCH-perf-20260909/build/`。

## 工程接续与保留的失败

启动回显最后一行受 PowerShell→SSH 的末尾 CR 影响，读取 PID 的命令返回失败；现地核对原 worker 已正常启动，未重复启动。实际 worker 213758、合同和 guard 均正常结束。

首次归档工具将 `/usr/lib/x86_64-linux-gnu/librt.a` 当作项目内文件，故在创建输出前拒绝；修正为明确记录外部依赖身份后归档成功。初始收集器源码也保留，没有重跑 GPU 合同。

后续新 worktree 的初次 factory 安装在编译前因 LF 与混合 CRLF 的原字节 SHA 不同而拒绝。已保留初次日志；修正仅在源码安装检查中显式核对 pinned Git 原文及去 CR 后逐字节相等，再安装合同已测试的原字节 payload。共享公式未变化，原日志／档案不做换行转换。第二次使用新 control 目录、全新 build，保留第一次失败。

本阶段只放行实际网络 factory 的编译和数值验证，不是三 ODE、Helm 全应用、正式性能或 sanitizer 通过；生产 `src/` 尚未启用该候选，双方协作分支发布的是开发检查点。

发布复核补充：仓库通用 `build/` 忽略规则会漏掉原路径中带 `build` 的文本证据。首次 Git 检查只验证已跟踪文件，未检查全集，因此旧 `git-byte-audit-v1.json` 仅是子集字节检查。随后显式补入这些已校验的原文本，并将审计收紧为“本地文件全集＝提交文件全集＋逐字节一致”，增加三项合成回归。两端 raw 归档始终完整，不曾丢失或改写原数据。
