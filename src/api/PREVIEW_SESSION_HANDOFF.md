# 持续预览与资源复用：Core 交接

交付分支：`codex/studio-core-ui-contracts`。本次在 `97a2b50c440473dfe93ab82617c5631e8b9da24a` 后追加，main 冻结基线仍为 `01cc4f723e674d47fe23850e7c0fef221e92e98c`。没有合入前端分支历史。

## 本次 Core 已完成

1. `--preview-session`：Linux/WSL 本地 CPU 会话，通过单行 JSON 连续提交未保存的参数文本；返回进度事件和嵌套原有响应。旧单次命令保持兼容。
2. 资源复用：最多保留一份 EOS 表，组分数据由缓存自己拥有；每次配置和模型重新建立。内容、类型或组分改变时重新加载。
3. 完整内容校验：每个请求前后仍读取完整来源。最多保留 128 MiB 文件字节，字节完全相同时复用 SHA-256；同大小、同修改时间的内容变化仍识别。大文件超出缓存预算时正常计算摘要。
4. 精确采样复用：每个采样坐标执行真实 Init，同次请求中逐位相同的初始输入共享 EOS 转换结果。最多 1024 种状态、1 MiB 键和值负载。无近似比较，不沿用上一请求场值。
5. 阶段、耗时、请求身份和资源计数；请求失败清空缓存。整体地址空间、累计 CPU 计时处理、协议大小和请求数量均有限制。
6. 保留既有 IdealGas / HelmEos / Tabular 回调类型；通过 CPU 编译约束检查后端调用兼容性，不需要前端适配 EOS 类型。

模型公式、热点迭代次数、收敛条件、正式 AMR 初始化和运行时科学逻辑未改变。完整场图/网格仍支持 Sod 1D 与 CellularDet 2D；其他已注册模型沿用统一初始化检查。

## 合作者接下来完成

| 负责人 | 工作 | 验收要点 |
|---|---|---|
| Host | 检测会话能力，管理长期存活的 CPU 进程，读取 NDJSON | 不在每次改参时启动新进程；旧 binary 可用原单次流程 |
| Host | 一个执行中请求，加一个最新待处理参数版本 | 不堆积每次按键；过期请求的成功结果不能替换当前版本 |
| Host | 墙钟超时、取消和进程回收；会话上限后的重建 | 取消终止整个会话，下次重新准备；阶段消息不续期超时 |
| Host | 项目、运行目录、binary、进程代号、配置与请求身份 | 切项目/切构建后不能接纳旧进程结果；场图和网格核对 EOS 来源 |
| Studio | 首次准备、正在更新、完成、失败/取消的界面状态 | 按实际阶段展示，可取消；不显示虚构百分比 |
| Studio | 输入合并与拖动反馈 | 防抖可先约 300 ms；拖动指示即时变化，提交后更新真实场 |
| Studio | 旧图保留与 Inspector 配对，新结果整体替换 | 旧图明确标记过期，不混用新参数和旧数据；过期 AMR 不叠到新场上 |
| Studio | 显示操作直接重绘 | Log/Linear、配色、显示上下限、截断、已有数据缩放不调用初始化 |

独立桌面窗口、命令行启动器和原 AMR 绘制等需求继续由 Host/Studio 实现。统一要求已写入 [ARCH_STUDIO_LOCAL_LAUNCH_AMR_UX_REQUIREMENTS.zh-CN.md](../../docs/development/ARCH_STUDIO_LOCAL_LAUNCH_AMR_UX_REQUIREMENTS.zh-CN.md) 第 10 节；可直接转交此文档。

## 实测样本

本机 CPU Debug，CUDA OFF，工作进程一条 OpenMP 线程；每项为单次样本。每个模型首次使用新会话，后两次沿用同一会话。Core 处理时间，不含 Host 排队和界面绘制。

| 工作量 | 首次 | 修改位置 | 修改温度或层级 |
|---|---:|---:|---:|
| CellularDet，128×128 完整初始场 | 6.84 s | 0.451 s | 温度：0.479 s |
| CooperativeHotspots，完整 Setup 与 9 点检查 | 6.02 s | 0.039 s | 温度：0.041 s |
| Sod，真实初始 AMR，小规模参考 | 8.04 ms | 5.61 ms | Level：7.68 ms |

CellularDet 全部 16,384 个坐标都调用了 Init；对应 37–38 种逐位不同的状态。对所有采样状态都不同的连续场，或大规模 AMR，仍需要完成相应计算，不承诺统一刷新时长。首次载入测量不代表系统磁盘缓存为空。

原始阶段数据和可重现脚本位于 [examples/preview-session](examples/preview-session/README.md)。源码变更、错误、取消、主动释放缓存和会话回收后，下次请求可能再次出现首次准备耗时。

## 同步与调用

已包含 `97a2b50c` 的分支只需摘取其后的本次提交。仍停留在 v0.13.0 / `e97e571ba98641384aee44425a29b83255401856` 的分支，应先摘取 `97a2b50c`，再摘取本次增量，然后重建 CPU binary。准确提交由交接消息与远端分支提供；不需要合入 main 或整条 Studio 历史。

先读取 `--preview-capabilities.extensions.session`，再使用 [PREVIEW_SESSION_API.md](PREVIEW_SESSION_API.md) 的协议。旧 response 对象直接作为 session-result.response 返回；现有场图、AMR、参数检查的响应处理器可以继续使用。

测试命令：

```sh
ctest --test-dir build-ui-api -R '^(preview_session_contract|preview_verified_resources|preview_exact_sample_cache|tabular_eos_ideal_gas)$' --output-on-failure
```

已使用独立临时 Git 索引核对：v0.13.0 checkpoint 先应用 `97a2b50c` 对应补丁后，本次完整补丁可以直接应用。

新增测试覆盖连续改参和独立进程数值一致、实际 Helmholtz/3D/4D 表、旧组分对象销毁后的绑定、同长度同 mtime 文件修改、缓存预算、错误恢复、取消重启、协议限制和 CPU 预算恢复。二维场还与独立直接 Init/EOS 参考对照，初始网格继续与正式 CPU t=0 初始化对照。最终验证结果见下方验证记录。

## 验证记录

2026-09-21 最终 CPU Debug 构建通过，CUDA OFF、KLU OFF、OpenMP ON；工作进程限制为一条 OpenMP 线程。18/18 组 scoped CTest 全部通过，最终整套耗时 280.11 秒。

- 新增 `preview_session_contract`：8 项真实进程测试，包含 Cold/Warm 的 Sod、CellularDet、CooperativeHotspots、AMR、内容变化、错误恢复、取消重启和协议边界；最终 71.08 秒。
- 新增 `preview_verified_resources`：完整字节验证、同 mtime 修改、超缓存预算、清理与作用域恢复、短请求后的 CPU 预算恢复。
- 新增 `preview_exact_sample_cache`：每个物理输入及组分、单 ULP 改变、正负零、失败和容量上限均不会误用结果。
- `tabular_eos_ideal_gas`：既有数值与来源检查，加上会话组分生命周期、组分重排、表变化、3D/4D 切换及原 EOS 回调类型编译约束。
- 既有 14 组：preview_initial_conversion、preview_api_contract、configuration_api_contract、preview_parameter_reads、preview_parameter_metadata、preview_sampling_limits、preview_cellular_2d、mainline_authority、ui_expansion_contract、refinement_indicator_math、amr_operation_plans、topology_transaction、initialization_probe、case_inspection_contract。

二维完整回归的 10 项测试全部通过，含两个方向的直接 Init/EOS 对照。AMR 继续通过正式 CPU t=0 网格对照。性能统计放在会话外层，旧场图 execution 对象保持原结构。

本机记录：`build-ui-api/session-final-build.log`、`session-release-tests.log`、`session-benchmark.json`。`session-final-tests.log` 是修正旧响应结构前的中间记录；最终结果以 `session-release-tests.log` 为准。

未进行 CUDA 编译或 GPU 测试；Host/Studio 接入、桌面启动器和界面 UAT 由合作者完成。
