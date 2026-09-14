# S5a：AMR 指标跨块批量化（数值通过，性能待验收）

基线为已发布的 `32cc416220a69cad5ab12030ba7b26db360bcd22` 燃烧／扩散 kernel 批量版本。
本阶段不更改物理公式、指标阈值、浮点运算顺序、EOS、网络、ODE 或 restart schema。
完整构建与本节规定的全套数值回归已完成；这次同步是数值合格的开发检查点，
不是 S5 最终性能验收，也不是整个大网络／安全收尾完成。

## 修改及边界

- Host 仍选择指标、校验 Current slot／handle／storage generation／topology epoch 并决定 regrid。
  CUDA 只消费显式绑定，调用原共享热力学、指标叶函数和原有顺序 maximum。
- 单波最多 1024 块。错误数组、热力学和组分共享一个可复用的高水位 arena，
  可选跨块 scratch 上限 64 MiB；若必需的单块已超限则仅处理一块。
  这不是总显存上限：状态、元数据、输出 summary 和必需的大单块内存另计。
- 每波 3 个 kernel（status reset／indicator／maximum），需要热力学时为 4 个；
  整批一次 summary 回传与完成等待。波间只按原 stream 顺序复用私有区域。
- 每块保留独立 EOS latch。不跳过固定层级的指标计算，也不丢弃 NaN 或中间 EOS 失败。
  全部绑定先校验，再提交设备写操作。

## clean Release 构建

服务器：GPU-273312，H100-20C vGPU，CUDA 12.8.93，GCC 11.4，sm90，严格浮点。
真实独立 Git worktree 为 `/home/ubuntu/projects/ARCH-s5-indicator-20260914`，
checkout 上述提交后应用记录的六文件 overlay；不是复制二进制或修改 CMakeCache 冒充重建。

完整 backend archive、ARCH、validator 及测试目标已成功链接。
四个编译作业的进程树 peak RSS 为 8,984,956 KiB，构建墙钟 1563.245 s。
当时另有独立大网络 CPU 构建与 GPU 数值诊断，故该墙钟不是独占编译基准，
该 RSS 也不是单 TU 峰值；更不能据此认定 16 GiB Debug 机器合格。
guard 未停止；swap 已用量从 8,232 增至 17,960 KiB，没有“完全不使用 swap”的声明。

| 产物 | SHA-256 |
|---|---|
| ARCH | `64e8bf0ac6f0ad85f1a930d166b8d1a31b71c62bfa2d20468154c578f6ec593e` |
| arch_cuda_backend archive | `3874211389634815a756da26f811231cd5f836d2fa78ebed7dbe9fe7ef6e57e4` |
| checkpoint validator | `2a3492b170f00fbd196659e1f83c8e84e5c45a483790831fcbee0a48b5913f85` |
| 六文件 overlay | `0e967f94b5cfb67eff60c339ef1d3fd6d79dd15c94bec45aa901064da52c9dc7` |

## 验证协议

新增叶测试已经真实运行通过，覆盖 1／3／1024／1025 块、波复用、异构 extent、
九种维度／几何组合、所有指标字段、热力学与非热力学混批、NaN 与有效块双顺序、
Tabular3D／4D 中间 EOS 失败与有效块隔离，以及 scratch 容量／溢出边界。
生产 backend 测试还检查 grow／reverse／shrink、Euler／RK2／RK3 slot rotation，
以及重复／过期／非 Current 绑定在提交前拒绝；指标与逐块基准作逐位比较。

整套数值流程已按原预算全部通过：contracts、batch-contracts、canonical、independent、
first-law、NSE、coupled、全部输运 coupled、AMR、curved、lifecycle、restart、
两组 coupled restart 和 tails。15 个 `record.json` 均为 passed 且运行后身份核对成功；
测试前后 ARCH／archive／validator SHA-256 一致。曲线矩阵含 24 算例、96 次运行，
生命周期含 3D 80 步及 1D 100／500 步等原协议，不只测试一次 refine。

Linux 工具测试 352 项中 351 通过、1 跳过（15.417 s）；跳过项不算通过。
构建日志提取出 102 次已完成的编译调用，无失败；最大单调用为 Tabular4 Hydro，
4,119,060 KiB／1110.55 s，见 `build-metrics.json`。统计口径仍受上节并发条件限制。
汇总器另用已归档的 P1/P2 真实样本回归，并验证拒绝 pilot、缺样本、工作量失配和虚假统计，2 项单测通过。

## 证据与归档

`validation/` 提供逐阶段记录；`details/` 保留嵌套算例、restart、守恒及比较 JSON；
`build/` 保留编译命令／日志、源文件散列、工具测试和执行脚本。
大型 HDF5、全部文本输出及真实 Helm 表在原始压缩包中，未将这些数据替换成合成结果。

原始包名 `s5-numeric-validation-v1.tar.zst`，SHA-256：
`658ec5c364fde845d64fa2f6af56ab04cfd02b977781915c45894e3e0370deff`。
compact 包名 `s5-numeric-compact-v1.tar.zst`，SHA-256：
`2bee58bace7b9db39467f978cc5eadbfbf6257e37fb7c30bba635b8f4aa3ed84`。
服务器目录为 `/home/ubuntu/projects/ARCH-s5-indicator-20260914/build/`；
本地备份目录为 `C:/tmp/ARCH-perf-20260909/build/`。
两个包均已完成本地下载且与服务器 SHA-256 一致，见 `archive-copies.json`。
`raw-files.sha256` 记录原始成员散列，压缩前后已核对源文件不变。
六个待提交源码／测试文件还逐一与 overlay 及实际构建的 `source-files.sha256` 核对：
只有 CRLF→LF 的 Git 换行归一差异，归一后内容完全一致；没有将不同原始字节冒称相同。
原 CTest JSON 和编译日志的尾部空格刻意保留，不为样式检查改写原始证据。

## 尚未完成的资格

数值门槛已经通过；性能测试等待编译／其他诊断结束后执行。对照原已冻结的联合批量版本，
燃烧三 ODE、扩散 RKL1／RKL2 和六种全部输运耦合分别扫描 8／32／128 初始块；
candidate CPU 1／8／16 线程、baseline CPU 8 线程，两侧 GPU Host 固定 8 线程。
pilot 与至少一次预热＋五次交替正式测量分开；每次都检查原科学预算与实际步数／regrid 工作量。
统计口径为启动至退出，包含 I/O，不宣称纯 kernel 或已认证稳态性能。

vGPU 的调试限制仍使 compute-sanitizer memcheck／racecheck 资格受阻，不发布完整安全验收标签。
