# Plotfile 读取适配对接：当前候选实现

## 精确引用与交付边界

- Owner contract：23ff77c4f08419de2b3c5eadee214da2af25784e，已完整阅读，未 merge。
- 本地分支：studio/compute-optim-integration；尚未 push，不能称为远端可获取。
- writer / Driver 发布修复 / EOS 属性源码：568026113e09ebeb03eb0b92a55fa35096660d4e。
- Host / Viewer EOS 属性消费：f86f9706756277550d43c5ab0b035140abfa358f。
- 本文件所属提交另含 FP64 signed-zero 传输与显示补丁，见 PlotfileFp64Wire-20261003.zh-CN.md。
  获取这一提交的完整 SHA 应使用 git log -1；对外交付时附准确 SHA。
- 当前真实 EOS 属性文件的 CPU binary SHA：
  325ef7806af16e85c6dbfc545937da5e5bab32115e57ed5167a788296be4f91b。
  构建为 6c10cb2a 基线加记录在 PlotfileEosConstituents-20261003.Summary.json 中的 dirty writer inputs；
  不把文档提交 HEAD 冒充编译输入身份。

本轮范围仍是 Sod 1D 与 Cartesian CellularDet 2D AMR；曲线坐标、3D、XDMF 后续单独验收。
原始 H5/plt/checkpoint/ELF/full arrays/logs 留在本机 ignored studio/.local。

## 实际字段与数组映射

| 语义 | HDF 路径/属性 | 类型与排列 |
| --- | --- | --- |
| 时间/维数/几何 | 根 time / dim / geometry，time_unit | 标量；CGS time 为 s |
| 原始场 | /Data/<field> | FP64，1D [B,Nx]、2D [B,Ny,Nx]；i 最快，无 ghost |
| 场声明 | field dataset 的 metadata_version、unit、centering、basis、meaning、unit_reason | candidate-field-1；cell；未知值不猜测 |
| 笛卡尔中心 | /Grid/x、y、z；coordinate_unit、coordinate_basis | FP64[N]；field C-order 展平一致；cm/cartesian |
| 原生 bounds | /NativeGrid/x1_lower、x1_upper；x2/x3 同规则 | FP64[N]；inactive 坐标均 0 |
| 原生积分测度 | /NativeGrid/cell_measure | FP64[N]；实际 GridMetrics::CellVolume |
| 测度声明 | NativeGrid measure_unit / measure_normalization | 1D cm / per_unit_transverse_area；2D cm^2 / per_unit_transverse_length |
| block level / Morton | /Grid/level、morton | [B]；文件 block 顺序，不能按 Morton 重排 |
| file-local logical key | /NativeGrid/logical_x1、logical_x2、logical_x3 | uint32[B]；与 level 组合，仅本文件身份 |
| 来源身份 | /SourceIdentity 分散属性 | candidate-identity-1 / partial |
| 组分名 | /SourceIdentity/species_names | string[Ns]；runtime 顺序 |
| 组分属性 | /SourceIdentity/species_A、species_Z、species_gamma、species_Cv | FP64[Ns]；与 species_names 同序 |
| 属性声明 | species_properties_version / state / source / reason | checkpoint-species-1；recorded 或 unknown |

N=B*Nx 或 B*Ny*Nx。全局索引 1D b*Nx+i，2D (b*Ny+j)*Nx+i。
Owner adapter 可将 lower/upper 组合为 [B,Ny,Nx,2,2]，末两轴为 axis、lower/upper；
1D 对应 [B,Nx,1,2]；measure reshape 为 field shape；centers 保留三条展平数组。
不要求生产文件复制候选 JSON；读取适配层归一分散属性。

逐文件枚举 /Data，不要求全部字段都存在：
DENS rho，g/cm^3；PRES、ENER erg/cm^3；TEMP K；VELX/Y/Z cm/s；
ENTR 为 pressure_density_proxy，P/rho^Gamma1，unit unknown 并保留 reason；
ENUC specific_burning_energy_rate，erg/g/s；VORT/DIVV 1/s；组分质量分数 1。
标量 basis=scalar，Cartesian 速度使用实际分量声明；未知 ExtraPlotScalarField 不从名字猜语义。
单位共用 src/data/FieldUnits.h，原数组不做单位换算。
1D sum(rho*measure) 为 g/cm^2，2D 为 g/cm，不能标为三维质量。

## 身份和发布

case_id 来自 ConfigurationInput.case_id；raw_config_sha256 对实际 parser 字节；
binary_sha256 来自 Linux /proc/self/exe，scope=main-executable-only。
EOS/table/gamma/species 及四组属性取 resolved runtime checkpoint provenance。
run_id、effective_config_sha256、build_id、source_git_head 仍 unknown；不由文件名/当前 HEAD 填充。
EOS 属性精确记录不等于独立 EOS 科学认证。可信期望应来自文件外受控输入/构建记录。

同目录 .partial → write → flush → checked H5Fclose → atomic rename。
异常向 Driver 传播；只有成功返回才推进 plot index，保留旧正式文件并清理临时文件。
真实 Driver write/flush/close/rename/create 失败及同编号重试已测。
没有 fsync、真实 ENOSPC 或断电持久性声明；checkpoint 写出路径未因此改造。
complete marker 必须与发布流程证据共同判断。

## 当前查询与 Viewer

metadata、bounded raw slice、global/viewport LOD、physical native point；
Inspector 从 stored native bounds 定位单元并读取原始值，不使用 LOD 或插值。
gap 与 multiple match 明确失败。叶块 outline 最多 128 个，limited 如实显示，不合成粗层场值。
zoom/pan/Fit 重绘已有显示；viewport 细化为显式读取，保留 full-domain LOD、取消与 revision protection。
LOD 为坐标重叠加权 display mean，不是科学积分或原生值。

每次查询重做 whole-file digest，并扫描原生叶单元；没有跨查询 cache / spatial index。
固定 32×24 响应只限制返回量，Cellular 示例仍扫描 5120 单元。
真实 production worker 约 170–200 ms、观察到约 117–124 MiB 高水位，仅为这些小文件的证据，
不外推大文件或与演化同机的资源影响。rchar 含 Node/WASM，不是纯 HDF I/O；
read_bytes=0 不代表无逻辑扫描。启动期取消证据不代替 HDF 深度取消。
见 PlotfileIsolatedWorkerCost-20261003.zh-CN.md / Summary.json。

## 验证摘要与待 review

- 真 t=0 Sod DENS [12,16] / Cellular DENS [20,16,16] 与 checkpoint 原值逐位一致；
  四组 EOS 属性逐位一致。精确输入/文件/binary SHA 见 PlotfileEosConstituents-20261003.Summary.json。
- 另有真 Sod 9 字段 / Cellular 28 字段，37 字段 Host/client 与原生点回查通过；
  守恒量与组分逐单元对 checkpoint 位级一致，见 PlotfileCanonicalT0-20261003.Summary.json。
  这些多字段文件来自早期 b360c662 构建，不冒充后续 EOS 属性文件。
- center / measure 最大差 0；Cellular bounds 对 Preview 最大差 1.7763568394002505e-15，
  原样交科学 review，不新增容差。
- Linux native 已试用总览、缩放、平移、Fit、原生 Inspector 与 EOS 表格。
- 本轮 FP64 JSON 反例修复：316/316 tests、lint/typecheck/build PASS；未新增 native desktop UAT。
- 尚待 owner 完整读取 adapter、科学单位/基底/积分审查、PRES/TEMP/diagnostic 独立 oracle、
  完整身份、全域 AMR 无遗漏/重叠、大文件成本与同机影响等验收。
- owner 单点/header checker 通过不等于完整 AMR/科学验收。

可复现脚本：validation/io/verify_initial_plotfile.py、verify_plotfile_blocks.py、
verify_plotfile_fields.py、verify_plotfile_reader.mjs、run_driver_plot_publication.py、
measure_plotfile_query.mjs、measure_plotfile_isolation.mjs。
具体失败及处理后的摘要与脚本提交；原始科学输出不提交。


## 2026-10-04 大查询工程增量

两份合成 8192/524288 单元夹具补测读取成本，单次 query 内复用 bounds dataset 对象。
三组配对大总览/point 中位下降约26%，完整响应与真实 Sod/Cellular 文件对基线一致。
逻辑读调用仍高；没有 spatial index/cross-query cache。读取期取消/reap/recovery 已测，精确限定见
PlotfileLargeQuery-20261004.zh-CN.md / Summary.json。此证据不替代真实 AMR、科学 review 或同机演化。


## 2026-10-04 native y-face 修复

当前 writer 增量 b26fb8a2f5967db15aec1b4f2bcd3ddafb9689f1：仅将Cartesian y upper按共享整数面索引求值。真实Sod/Cellular新t=0文件全叶bounds精确覆盖通过，旧文件gap/overlap保留反例；原场值/中心/测度及checkpoint数值未变。schema/字段数组映射不变。新binary1bdd71ed344f01dd9722898479d37780e81a834f4d0bdea501b23a933804e1a8；详细身份和限制见 PlotfileSharedFaceRepair-20261004.Summary.json。本地提交尚未push。
