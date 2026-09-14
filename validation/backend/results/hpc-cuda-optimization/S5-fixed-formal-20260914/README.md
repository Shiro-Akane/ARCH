# 同一组分修复基线与 S5 候选：正式规模验证

本阶段尚在执行，不是 11 模块全部完成的性能验收报告。
扩散 RKL1／RKL2 已完成正式 216 次运行、210 次字段与工作量比较，全部通过；
详见 [扩散阶段汇总](diffusion-summary.zh-CN.md)。燃烧及六种全输运耦合正式计时继续执行。
两侧均应用经过数值验证的 coarse/fine 痕量组分闭合和 MUSCL 面归一化修复，
不使用原来数值失败的 128 块耦合结果计算加速比。

实际服务器源码／构建树：

- 基线：`ARCH-corrected-fused-baseline-20260914`，`32cc4162` 加同一八文件修复。
- 候选：`ARCH-multiphysics-fix-20260914`，`81c046f6` 加同一八文件修复。
- 两侧源文件指纹只差原 S5 的六个源／测试文件；完整配对记录在 composition-fix 的 `build-pair-v1.json`。
- 已完成原 15 类回归和六方法 × 8／32／128 块的 18 组扩展数值对照；它们不替代本阶段正式计时。

## 固定协议

扩散 RKL1／RKL2、燃烧 BE_NR／BD／ROS4、以及三 ODE × 两扩散法的六个全输运耦合组合，
共 11 模块，每项 8／32／128 初始块。
每个规模比较候选 CPU1／8／16、基线 CPU8、候选 GPU/Host8、基线 GPU/Host8；
一次预热、五次交替正式样本。每模块预期 108 次运行和 105 次字段／工作量比较。

口径为完整 ARCH 启动到退出，含初末 I/O，不含事后资格检查；不是纯 kernel、稳态或冷 OS 缓存计时。
不得并发编译或其他测试，不改变网络、EOS、ODE、终止时间、输出及原预算。
每项完成后归档原始 HDF／日志并核验服务器和本机两份 raw，再整理重复展开的 HDF；
不删除唯一数据、源码、二进制、日志或原始归档。

为控制磁盘占用，v2 归档配方仅在 compact／Git 投影中略去超过 2 MiB 的 TSV，
以 `omitted-large-files.json` 保留原路径、字节数和 SHA-256；完整原始日志仍在双份 raw 包中。
服务器逐文件无损压缩这些大 TSV，验证解压 SHA 与原文件一致后才移除展开副本。
每模块的 `trace-compression.json` 和 `hdf-compaction.json` 记录全部操作。
RKL1 使用原 v1 compact，未改写它；其 Git 投影差异另见 `git-projection-omissions.json`。
这仅改变完成采样后的存储方式，没有关闭 trace、减少输出或改变计时协议。

## 已冻结的实际产品

`products/` 保存两套真实 ARCH、backend archive、checkpoint validator、Helm 表、
源码指纹／补丁及配方的归档清单。原始二进制包已双端核验：

- `fixed-timing-products-v1.tar.zst`：297,792,308 bytes。
- SHA-256：`e33b410b002677271999f50f67b37992138e35308b7244a020c722dabb432027`。
- 服务器候选树 `build/`；本机 `C:/tmp/ARCH-perf-20260909/build/`。
- 包内 24 个文件，完整映射见 `products/manifest.json`；包外身份见 `products/archive.json`。

已经保存产品和配方不代表 11 模块正式采样已完成。后续各模块的结果与失败将分别保留。
超大网络性能及 vGPU sanitizer 限制仍单列，不用本阶段的内置网络结果覆盖。
