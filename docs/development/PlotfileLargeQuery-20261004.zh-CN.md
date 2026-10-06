# Plotfile 大叶块数量的工程查询与读取期取消

## 输入与边界

基线 33a49399cb5f1f671a9de3d575fc4137c989b271。
候选测量时为该基线加 Host 查询 dirty patch；精确文件 SHA、全部正负测量及检查结果见 Summary。
固定 Node v24.21.0、相同依赖；基线源码来自 git archive 的独立本地副本，不覆盖工作树。
本轮不 Build Core、不运行 simulation、不更改 scientific field/checkpoint。

新增 make_plotfile_query_fixture.py 生成 4 / 256 个 tiled Cartesian block，
shape 为 [4,32,64] / [256,32,64]，8192 / 524288 单元；
文件为 739328 / 46159872 bytes，contiguous FP64、无压缩。
这是独立合成存储 workload，不是生产 AMR topology、科学密度或发布认证。
DENS 仅沿用查询字段名，值为 linear index，unit unknown/reason 明确；
SourceIdentity 缺失；publication/native candidate 属性仅用于进入 reader 测试路径。
文件名由已有 harness 约定，不代表实际 HLLC 演化。
输出根目录必须不存在；实际重复创建已拒绝，既有文件 digest 未变。

## 发现与最小改动

固定 32×24 总览响应，大夹具仍完整扫描 524288 单元。
原实现每个 512 单元 batch 重新取得四个 bounds dataset 对象。
候选只在一次 open file / query 内复用 dataset 对象；批次、两遍扫描、
hash、返回预算、坐标、原始值、错误分类和取消流程保持。
没有 spatial index、cross-query cache，也没有将 display LOD 当原生场。

三组 baseline → candidate 顺序交替测量，同一 warm-cache 文件、每次 fresh worker：
- 大夹具 overview-0 中位 827.820 → 609.218 ms，约下降 26.4%。
- overview-1 中位 828.517 → 608.077 ms，约下降 26.6%。
- point 中位 723.804 → 530.374 ms，约下降 26.7%。
- 小夹具配对收益较小；初次候选 point 曾比初次基线慢，全部原始计时保留，不只报告正收益。

约 42029 bytes overview 响应不代表读取量小。
进程 rchar 仍约 650 MB，未因对象复用显著降低；不能宣称已解决读放大。
观察到约 120–130 MiB 级 worker HWM，不是硬 RSS/WASM 上限。
read_bytes=0 只说明此次 OS-cache 条件，不代表没有扫描；
rchar 包含 Node/import/WASM，不能当纯 HDF 或物理磁盘 bytes。
不外推压缩 chunk、大型真实 mixed-level AMR 或同机演化资源影响。

## 数值/身份/取消证据

verify_plotfile_query_responses.mjs 对两份夹具与既有真实 Sod/CellularDet EOS 属性文件，
分别经固定 production worker 返回 metadata / overview / point / slice。
16 个完整 numeric JSON response 的 SHA 和字节数与基线一致，
文件 read-before/read-after digest 不变。原始数组只留本机，不在报告输出。
真实文件准确身份见 PlotfileEosConstituents-20261003.Summary.json；
不把合成 workload 的 metadata 当实际 GridMetrics 或完整来源认证。

verify_plotfile_read_phase_cancel.mjs 保留严格定义：
观察到 worker 打开两个目标文件描述符且 rchar 超过两倍输入 bytes，才取消。
最终观察事件 236.728 ms，rchar 95119033，阈值 92319744；
240.737 ms 时 settle/reap，取消到 settlement 约 4.01 ms。
随后原生 point 恢复约 542.294 ms，value/index=384，文件 digest 不变。
BUSY、CANCELLED、/proc PID 消失及无遗留 worker 均检查。
这优于只在 startup 取消，但不是对某个 HDF 调用的精确仪器定位，
也不证明 kernel-uninterruptible I/O 或 native UI 取消全部成立。

## 检查与剩余工作

316/316 npm test，lint/typecheck/production build/diff check 通过；
三份新增脚本语法与实际执行通过。production asset index-CZj54lZc.js 未变，
现有 large-chunk 警告保留。本轮未新做 native desktop UAT。

原始夹具、日志和 baseline 源码副本留在
studio/.local/integration/plotfile-large-query-20261004；只提交脚本和处理后摘要。
完整科学 identity、独立 EOS/AMR review、真实大文件 chunk/index/caching、
同机运行影响、ENOSPC/持久性及后续几何仍未关闭。
Jeans/RZ 的 owner 科学待决项保持原状态；联合目标不因本次工程改进封箱。
