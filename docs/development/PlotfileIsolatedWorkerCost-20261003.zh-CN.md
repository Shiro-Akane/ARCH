# Plotfile production 隔离 worker 成本与生命周期

联合计划的独立 plt 排期，基线 1b3dcc5b86398d97fb0ddca0d942236b7a2a4ec0。
本次只新增只读测量工具，不修改生产 Reader/Host/Viewer，不运行 Core。
此前成本工具测进程内 reader，本工具调用实际 production isolated Host methods，
因此包括独立 Node/HDF WASM worker 的启动成本。

## 真实多字段文件

Sod 9 字段、55376 bytes、192 原生单元；
CellularDet 28 字段、1599616 bytes、5120 原生单元。
实际 canonical 文件与 binary/source 身份见 PlotfileCanonicalT0-20261003.Summary.json。
本次前后原始 SHA 相同，Host 返回 digest 一致；
两个 native point 经独立 h5py 位级回查相同。

## 实测（一次序列，不作性能排名）

| case | query | 返回 bytes | wall ms | observed worker HWM KiB | observed worker rchar | 扫描 cells |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Sod | metadata | 3673 | 169.40 | 119712 | 4413194 | None |
| Sod | overview-0 | 6339 | 174.54 | 119264 | 4446418 | 192 |
| Sod | overview-1 | 6339 | 181.22 | 120076 | 4446410 | 192 |
| Sod | point | 4593 | 169.04 | 120560 | 4419882 | None |
| Sod | metadata-after-cancel | 3673 | 168.32 | 119416 | 4413194 | None |
| CellularDet | metadata | 7520 | 179.03 | 120720 | 5975178 | None |
| CellularDet | overview-0 | 28512 | 199.52 | 127236 | 9000242 | 5120 |
| CellularDet | overview-1 | 28512 | 197.30 | 125404 | 9027018 | 5120 |
| CellularDet | point | 8487 | 187.58 | 125640 | 7957594 | None |
| CellularDet | metadata-after-cancel | 7520 | 175.66 | 122480 | 5975186 | None |

overview 使用 Viewer 同类 bounded request：1D 32×1，2D 32×24。
同一进程连续两次 overview 都启动新的 worker、全文件 digest 和全原生叶单元扫描。
Cellular 小返回约 28.5 KB 不代表小扫描，当前没有跨查询 cache。
此次 worker 启动成本与 HDF 分量没有独立拆分，不能从 wall 差值推出精确 startup 占比。

## 取消、容量和恢复

两文件均在确认真实 worker 存活后发起 cancel；
同时发起 metadata 请求返回 BUSY。
取消 promise settle 时匹配 worker 已退出，随后 metadata 成功，文件身份未变。
取消发生在启动早期（约 2.6–3.7ms）；明确不作为 HDF 深度扫描中的取消证据。
成功查询均通过生产 64 KiB 输出上限，原有 15s timeout/heap/file预算未扩大。

## 计数限制

每5ms轮询 /proc worker VmHWM/IO，报告 observed high-water，
可能错过退出前最终峰值及计数，不能声称精确峰值硬上界。
rchar 含 Node/import/WASM，并非 dataset-only bytes；read_bytes 本次0反映OS缓存，
不意味着未读取。先后 raw SHA 检查也会预热缓存，非冷缓存 benchmark。
没有实际 dataset instrumentation，也未验证大文件或同机 simulation 干扰。
没有进行 HTTP/native UI 操作或将成功查询变成 scientific acceptance。

## 复现与下一步

    node validation/io/measure_plotfile_isolation.mjs PROJECT_ROOT RUNS_JSON

使用项目 Node v24.21.0；RUNS_JSON 为 run_plotfile_fields_t0.py 产生的本机运行清单。
工具不提交原始数组，只生成 digest、点值、计时和过程摘要。
源码语法检查、真实12次查询/取消及两点独立 raw 复核通过。
测量之外生产未变，没有重复旧全套回归。

后续需真实大文件/index/cache、HDF深度取消与同机资源影响证据。
若采用缓存，必须另验来源身份及文件更换/修改后的失效，不能靠mtime冒充科学freshness。
