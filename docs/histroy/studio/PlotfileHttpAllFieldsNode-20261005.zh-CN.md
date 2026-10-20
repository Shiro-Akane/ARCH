# Plotfile 全字段 / 全原生叶单元 HTTP 读回节点（2026-10-05）

## 结论与范围
PASS：按 origin/codex/o8-boundaries 的 PlotfileValidationContract.zh-CN.md，
基线 f342a5593d29c5975d7957dafc064434e017d80a，补齐既有两份 Cartesian t=0 文件
在生产 HTTP 读取路径上的全字段读回证据。没有改 Core/Host/Viewer，没有生成新科学输出。

实际 openProject/createHostServer → 项目路径及 SHA pin → 隔离 reader →
validatePlotfileAudit（生产 client validator）；独立 h5py 直接读取原文件进行比对。
没有 fake response 或绕过 Host 的读取替代。此节点不启动 renderer，不扩充先前
两个 DENS 点的人工 UAT 声明。

## 覆盖
- Sod：9 fields × 12 blocks × 16 cells = 1,728 raw field values；192 active leaf cells。
- CellularDet：28 fields × 20 blocks × 16×16 cells = 143,360 values；5,120 active leaf cells。
- 共 145,088 场值、668 block/field slice + 2 metadata HTTP 请求；最大响应 42,737 bytes。
- 每个 field/block 必须恰好出现一次；独立检查所有 stored values 的 FP64 bits、
  x1-fastest linear index、Grid centers、NativeGrid bounds/CellVolume、level/logicalKey。
- 字段 unit/basis/centering/meaning 与原 HDF declaration 一致。ENTR 保持
  pressure_density_proxy，unit unknown → null，并保留 unit_reason；
  没有改标为热力学比熵。NaN/Infinity 若出现只能证明命名 token 语义，不证明 payload bits。
- case/raw config/binary 来源与 HDF 原属性一致；各 slice 身份与 metadata 一致。
  run_id=unknown 保留 null。actual producer 仍为旧 1bdd71ed...，不是当前 CPU/CUDA ELF。
- 两原文件 SHA 前后不变；Host 只监听 127.0.0.1 分配端口，结束后连接拒绝。
- 六项反例 raw-value（一 ULP）、array-order、cell-measure、field-unit、
  binary-identity、missing-block 全部被独立 oracle 拒绝，不放宽容差。

## 成本与诊断
本机顺序全字段读取：Sod 18.67s、CellularDet 107.42s。每 field/block 单独生产请求，
这是完整读回批任务时间，不能作为用户单次 Viewer 加载延迟或正式 benchmark。
返回预算不是首域扫描/hash/解压/RSS的上限。完整 trace/数组和失败日志仅本机保留。

独立 oracle 首次启动命令跨 shell 展开了错误 root，得到 FileNotFoundError；
该失败日志保留。改用明确 Linux absolute root 后复核同一 trace 通过，没有重跑
HTTP 或修改数据以掩盖失败。

## 复现
需要本机既有文件的 trusted launch JSON（oracle 包含 path/sha256）。
新目录必须不存在，使用项目 Node / h5py 环境：

    node validation/io/verify_plotfile_http_all_fields.mjs LOCAL_LAUNCH.json NEW_LOCAL_OUTPUT
    python validation/io/verify_plotfile_http_all_fields.py --root ABS_ARCH_ROOT --trace NEW_LOCAL_OUTPUT/trace.ndjson --output NEW_LOCAL_OUTPUT/verified-summary.json

本轮 raw 根：studio/.local/integration/plotfile-http-all-fields-20261005/。
反例在本地 trace 的首个 slice 分别改动一个 value/index/measure/unit/binary；
遗漏反例删除最后一个 slice。全部只改 trace，不改原 HDF。
处理证据：validation/io/results/plotfile-http-all-fields-20261005/summary.json。

## 未宣称完成
只证明存储 → HTTP 原生读取的一致性；字段科学意义/EOS/积分守恒仍需 Core review。
不覆盖演化文件、新 producer freshness、全字段 renderer UAT、完整 App、取消/race、
writer ENOSPC/close/rename 故障、首域大规模内存成本或独立物理 oracle。
JENS/RZ 门槛、冻结长包及全项目科学签收保持开放；Linux/WSL only。
