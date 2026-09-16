# 同一组分修复基线与 S5 候选：正式规模验证

本阶段 11／11 模块正式矩阵已完整通过；[阶段总表](all-modules-summary.zh-CN.md) 区分本轮性能结果与仍待完成的大网络优化。
扩散 RKL1／RKL2 已完成正式 216 次运行、210 次字段与工作量比较，全部通过；
详见 [扩散阶段汇总](diffusion-summary.zh-CN.md)。三种燃烧方法也已完成正式 324 次运行、315 次比较，
全部通过，详见 [燃烧阶段汇总](burn-summary.zh-CN.md)。
第一组 [BE_NR＋RKL1 全输运耦合](coupled-be-rkl1-summary.zh-CN.md) 也已完成 108 次运行、105 次比较及双端备份。
第二组 [BE_NR＋RKL2 全输运耦合](coupled-be-rkl2-summary.zh-CN.md) 完成另外 108 次运行、105 次比较及双端备份。
第三组 [BD＋RKL1 全输运耦合](coupled-bd-rkl1-summary.zh-CN.md) 也已完成 108 次运行、105 次比较及双端备份。
第四组 [BD＋RKL2 全输运耦合](coupled-bd-rkl2-summary.zh-CN.md) 完成另外 108 次运行、105 次比较及双端备份。
第五组 [ROS4＋RKL1 全输运耦合](coupled-ros4-rkl1-summary.zh-CN.md) 完成另外 108 次运行、105 次比较及双端备份。
第六组 [ROS4＋RKL2 全输运耦合](coupled-ros4-rkl2-summary.zh-CN.md) 也已完成 108 次运行、105 次比较及双端备份。
合计 11／11 模块、1,188 次运行、1,155 次比较通过，不等于超大网络优化或 sanitizer 全部验收。
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

完整采样及身份核验现已完成；各模块全部样本和独立失败记录均保留。
超大网络性能及 vGPU sanitizer 限制仍单列，不用本阶段的内置网络结果覆盖。

一次 SSH 连接重置后，已接回当时仍在运行的第一项耦合实验；该组现已完整结束、归档，后五项使用独立进程控制。
见 [断线恢复检查点](recovery/README.md)；只调整运行管理，不重启当前样本或修改数值／采样规则。
随后依据长单线程样本，对尚未启动的后五组将单次执行 wall 上限从 1800 调至 3600 秒；
这是明确记录的运行护栏修订，物理输入、数值预算与重复数均不变，第一组仍使用原上限。
详见恢复检查点中的 15:25 UTC 修订及逐项工具测试。

## 证据完整性复核

`integrity-audit-diffusion.json`、`integrity-audit-burn-all.json` 重新检查各模块完整样本门槛、
前后身份、本机 raw SHA、Git 投影文件 SHA 以及 HDF／trace 后处理清单。
第一组耦合的同类复核见 `integrity-audit-coupled-be-rkl1.json`。
第二组见 `integrity-audit-coupled-be-rkl2.json`，前七组全量回读见 `integrity-audit-seven.json`。
新增第三组和前八组全量回读见 `integrity-audit-eight.json`。工具会话中断后仅收集已成功的
服务器独立 worker，未重跑该组；回执随第三组证据保留。
这是存储及报告复核，不是新的独立物理或 sanitizer 验证。
第四组及前九组的同类复核见 `integrity-audit-nine.json`；原队列自动接续后两组，未重采样。
第五组及前十组全量回读见 `integrity-audit-ten.json`；只计入首轮 I/O 中止后重新完成的 108 条样本。
第六组及十一组全量回读见 `integrity-audit-eleven.json`；原 BE 补测启动前另通过同范围的 `integrity-audit-all-pre-be.json`。
`recipes/` 保存配方及八项含篡改负例的检查器测试。
`git-blob-audit-diffusion.json` 另核对发布提交 `b80efc92` 两扩散目录的 1,438 个文件，
本地原件与 Git blob 字节一致；原始大包的 SHA-256 校验单独记录。
`git-blob-audit-coupled-be-rkl2.json` 同样核对 `6534c969` 第二组耦合的 725 个文件，
总计 35,177,678 bytes，与本机原件逐文件一致。
`git-blob-audit-coupled-bd-rkl1.json` 核对 `35278ff9` 第三组的 729 个文件，
总计 35,090,912 bytes，与本机原件逐文件一致。
`git-blob-audit-coupled-bd-rkl2.json` 核对 `21d3d4b0` 第四组的 726 个文件，
总计 35,130,888 bytes，同样与 Git blob 字节一致，未归一化原始换行。
`git-blob-audit-coupled-ros4-rkl1.json` 核对 `d88beea0` 第五组的 726 个文件，
总计 35,116,367 bytes，与本机原件逐字节一致；失败首轮仍在独立恢复目录，不计入本组。
`git-blob-audit-coupled-ros4-rkl2.json` 核对 `525d595c` 第六组的 726 个文件，
总计 35,163,730 bytes，与本机原件逐字节一致；原始包身份仍由独立 SHA 校验记录负责。

## 阶段间容量恢复

第二组完成后，下一组 BD/RKL1 首次 preflight 因磁盘不足原 8 GiB 门槛而退出，未启动样本。
现已保全该记录，仅在无活动采样的间隙核验并移除旧 S4 的 384 个冗余展开 HDF，原始包保留两端。
恢复到原容量门槛后接续后四组，没有重跑前七组或降低护栏；
详见 [实际操作与身份记录](storage/capacity-recovery-20260915-v1/README.md)。

ROS4／RKL1 首轮随后在 80 条完成运行之后触发原系统 I/O 压力护栏，未计入正式通过总数。
完整保全、双端备份、迁移旧目录及安静检查后，按同一冻结协议从头重跑全部样本；
不把首轮与重试拼接。详见 [I/O 护栏中止与接续记录](recovery/io-pressure-20260916-v1/README.md)。
重试现已全部通过并双端归档，随后 ROS4／RKL2 也完整通过，原队列已接续 BE 长轨迹补测。
重试中的 SSH 暂时失联未中止独立 worker，未再次重跑样本。

## 已有计时字段的派生分析

[regrid 调用区间分析](regrid-intervals-note.zh-CN.md) 汇总前三组已完成耦合算例的 90 条既有
GPU 正式样本。128 块 S5 的该入口区间约占应用墙钟的 12%；这不是独占 AMR kernel 时间，
也不涵盖全部 AMR 操作。现有字段不能补出燃烧／扩散的完整逐步或稳态计时。
该分析没有新增服务器运行，本身不增加已验收模块数；原始样本与采样协议均未改动。
