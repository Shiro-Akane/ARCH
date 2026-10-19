# Plotfile 真实 t=0 输出对照

日期2026-10-03；clean source基线24f448ce1c82248802fbee640e7db225d048d23a。
范围：现有已批准Sod/CellularDet t=0初始化输出，非演化/全字段验收。

## 构建与实际命令

既有build-cpu CPU Release增量cmake --build build-cpu --target ARCH -j8，
约23.41秒，0.5秒间隔采样整个owned后代进程RSS峰值约4.80GiB。
WSL可见28CPU/约21GiB available；本轮8jobs，未为并发优化重复编译。
采样RSS相加包含共享页重复计数，不冒称精确物理峰值或最大可用并发。
编译前后SHA与clean source身份见Summary，新的binary SHA：
a5d3467297188775069d9c466cdfacb0dfa3143ec7a834de714446c12a248308。
本机持久ignored目录存ARCH-t0副本、输入、H5/checkpoint/API响应和原始日志。
build是工程成功，不代表LTO完整依赖/构建freshness缺口已关闭。
桌面当前build-studio-cpu production binary未替换。

精确复用现有tests/api/preview/test_ui_expansion.py
Expansion.test_mesh_matches_production_initial_topology，
使用原Sod与CellularPreview2D输入、原refine阈值、max_blocks与EOS表。
既有测试明确tmax=0/max_steps=-1，checkpoint证明time=0/step=0。
唯一输出目录由既有test创建，未新增演化轨迹或放宽科学门槛。

## 数据结果

| 模型 | 叶块 | DENS值 | checkpoint bit mismatch | center差 | measure差 | bounds最大绝对差 |
|---|---:|---:|---:|---:|---:|---:|
| Sod 1D |12|192|0|0|0|0|
| CellularDet Cartesian 2D |20|5120|0|0|0|1.7763568394002505e-15|

Plotfile/Checkpoint/Preview logical keys集合逐项相同。
block顺序通过level/logical_x1/x2/x3映射，不假定同一存储排序。
DENS使用uint64 bit-pattern比较；不是只比较浮点数值或放宽近似误差。
原生bounds/center/measure对照authoritative AMR lower/cellShape/spacing。
记录完整最大算术差异；没有新增科学容差或把二维bounds差异擅自定为通过。
原始数据值、物理单位与低维测度含义仍交Core review。
case/raw config SHA/actual binary SHA/EOS和ordered species与实际文件/运行身份一致；
读取前后Plotfile文件SHA不变。未知build/effective/unit系统保持unknown。

真实文件Node bounded reader→client validation通过，1D3/2D6样本，
返回2676/3365 JSON bytes；该返回大小不等于全域HDF实际read bytes。
仍renderEligible=false/completion unknown，正式科学认证没有自动提升。
可重复只读验证脚本validation/io/verify_initial_plotfile.py，
用--project-root/--evidence-directories/--binary/--output指定本机证据；
仅生成处理摘要，不上传原始数组。

## 验收界限

已有t=0方法1/1（两个case）PASS，独立逐值核对通过；这是新增真实writer证据，
不重复无关已通过Studio283或configuration/IO单元检查。
未运行正式timestep/evolution/CUDA；没有PRES/TEMP/ENER全字段结论。
二维非方形query此前只有manufactured测试，此次真实Cellular块为16x16，不补称非方形科学验收。
全域/局部Viewer、zoom/pan/AMR轮廓/LOD/native desktop UAT、
单位定义、I/O/RSS测量及科学owner review仍未完成。
固定image尺寸只限制响应，首次全域可能扫描大量叶块。
本轮无push/tag/main merge/Windows适配。
