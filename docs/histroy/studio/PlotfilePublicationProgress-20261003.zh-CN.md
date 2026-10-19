# Plotfile 候选发布机制进度

日期：2026-10-03。基线：835edb772064f24d2ac020a6a30a13364d4f3297。
授权依据：联合计划 35c5b7b114069621901386bfc4bc2a656e65af06 §4.3。
状态：共享 writer 发布基础增量；不是首个交付全部完成或科学语义已冻结。

## 文件语义

保留 time/dim/geometry、Grid/x,y,z/level/morton 与 Data 原始 double 数组、
[blocks,Nx] / [blocks,Ny,Nx] / [blocks,Nz,Ny,Nx] 排列，不排序 block、不转精度。
新增 root 属性 plot_publication_version=candidate-1、
plot_publication_state=complete、plot_publication_method=checked-close-atomic-replace、
plot_storage_order=x1-fastest、plot_identity_state=unknown。
未知科学身份不能由这些发布标记或文件名补齐。现有审计 reader 的
renderEligible=false / completion unknown 不因本轮改动自动提升。

Linux/WSL 同目录 mkstemp 临时文件；写完整后检查 flush、子对象释放、H5Fclose，
再通过 rename 原子替换最终路径。保留旧成功写入的 overwrite 行为。
Saved 日志移至发布成功之后，异常向调用方传播，不再吞掉。
发布前失败保留旧最终文件；尽力删除临时文件。kill/crash 可能遗留 partial，
reader 不应把 partial 或单纯存在的文件当作正式结果。
原子可见性不代表断电耐久性，本轮没有 fsync 保证。
候选暂存权限为 mkstemp 的私有权限；多用户共享策略后续 review。

## 验证

仅 CPU scoped target 编译，未编译 production ARCH、未运行 simulation。
CTest plotfile_publication 与 checkpoint_compatibility：2/2 PASS。
覆盖 Sod-shaped 1D、非方形 2D 两块、存储顺序与 raw double/NaN/Inf、
成功替换、无效长度拒绝且旧数据不变、缺失父目录创建失败、
目标为既有目录时 rename 失败且目录内容不变、临时文件清理。
checkpoint 格式与实现未修改，既有兼容性 suite PASS。
第一次测试编译使用不存在的 HighFive read_raw，改为实际 read API 后通过。
新增测试触发既有 build-cpu 的 CMake 自动 regeneration，无独立新 build tree。

尚未覆盖真实 write/flush/close 故障注入、SIGKILL 崩溃与并发 reader 时序；
不把成功路径检查冒称这些 failure tests 已通过。
科学身份、单位、native bounds/CellVolume 与真实 Sod/2D AMR 逐值对照待继续。
本地原始 fixture/日志位于 build-cpu 与 studio/.local/integration；
不提交 H5、完整数组或原始日志。

## 后续

补原生网格与身份候选 metadata，使用 GridMetrics::CellVolume；
接只读原生单元查询后交付准确 SHA、数组映射与处理后的验证摘要。
之后接 Native AMR / Displayed LOD、全域/局部显示与 Inspector。
首次全域总览可能扫描大量叶块，实测 I/O/响应/峰值内存。
曲线、3D、XDMF 不属首版，不更改数值推进/checkpoint。
