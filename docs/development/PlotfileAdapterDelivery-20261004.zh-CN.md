# Plotfile 当前适配交付索引（2026-10-04）

本索引按 owner 的 PlotfileValidationContract.zh-CN.md 核对，优先于旧 handoff 中按时间保留的历史引用。
仅交付 Sod 1D 与 Cartesian CellularDet 2D AMR 小切片，不宣称全项目或独立科学验收通过。

## 精确引用

- Owner contract：23ff77c4f08419de2b3c5eadee214da2af25784e；已完整阅读 contract 与实际验证器。
- 当前累积实现源码：3f3e0a0ecb5b97e14afe853785e487a95b0987d1。
- 本地分支：studio/compute-optim-integration；核对前 clean，尚未 push。该 SHA 不是远端可获取引用。
- 当前 main CPU ELF SHA-256：7d0360de4ac9a3429d2a8ffec3908ca4424f2c7716096f2c44ad189662a9d8c4。
- 本次只读抽查使用既有 CurrentCpuPlotfileT0-20261004 的两个文件，生产源码为
  869ae3d3d30e8a9b16e377112af73e737b6c736a，生产 ELF 为
  f82bb7ff16c4acf54ae84970b9b403dce3d0370a468d241169519b3bfd1f6a44。
  这些样本的 producer 身份不得替换成当前 HEAD 或 ELF。

结构化字段、属性、源身份和单点摘要见 [PlotfileAdapterDelivery-20261004.json](PlotfileAdapterDelivery-20261004.json)。
读取适配不要求 writer 复制一份 plotfile_candidate JSON。

## 字段与数组

| 内容 | 生产映射 |
| --- | --- |
| 原始场 | /Data/<field>，FP64；1D [B,Nx]、2D [B,Ny,Nx] |
| 中心 | /Grid/x,y,z，按 field C-order 展平 |
| 原生 bounds | /NativeGrid/x1_lower,x1_upper；x2 同规则，展平 |
| 测度 | /NativeGrid/cell_measure，来自 GridMetrics::CellVolume，展平 |
| 块身份 | /Grid/level,morton 及 /NativeGrid/logical_x1,x2,x3，均按文件块顺序 |
| 来源 | /SourceIdentity 分散属性；含实际 case、原 config SHA、运行 binary SHA、EOS、output-session UUID |
| 场声明 | 每个 field dataset 的 unit、basis、centering、meaning、unit_reason |

全局单元索引：1D b*Nx+i；2D (b*Ny+j)*Nx+i。i 最快，只有活动叶单元，无 ghost。
不能按 Morton 重排后直接索引字段。
Adapter 可组合 bounds 为 [B,Nx,1,2] 或 [B,Ny,Nx,2,2]；
末两轴为 axis、lower/upper；measure reshape 为原 field shape。

DENS：g/cm^3；PRES/ENER：erg/cm^3；TEMP：K；VELX/Y/Z：cm/s；
ENUC：erg/g/s；VORT/DIVV：1/s；species mass fractions：1。
Cartesian 速度为 cartesian basis，标量为 scalar。
ENTR 是 pressure_density_proxy，P/rho^Gamma1，unit unknown，保留原因。
按文件实际枚举字段，不用固定字段数推断内容。
1D measure 为 cm/per_unit_transverse_area，2D 为 cm^2/per_unit_transverse_length；
sum(rho*measure) 分别为 g/cm^2 与 g/cm，不能标成三维总质量。

## 验证及限制

本次仅只读抽查既有真实文件：Sod [12,16]、9 字段；CellularDet [20,16,16]、28 字段；
分别读取原生末单元 [11,15] 与 [19,15,15] 的 DENS、bounds、measure、level/Morton。
文件 SHA 前后不变。没有新 Build、simulation、输出、完整数组提交或重复 baseline。

已有工程证据：
- PlotfileCanonicalT0 / PlotfileAllFieldsFaceRepair / CurrentCpuPlotfileT0：37 字段与 checkpoint 原值位级一致。
- PlotfileEvolvedReader：既有 Sod 连续/续算 8 文件、128 次 production native point 与独立 h5py 存储值一致。
- PlotfileDriverFailurePropagation：实际 Driver 的 write/flush/close/rename/create 故障向上传播，
  失败序号不推进，成功仅推进一次，同编号重试；原正式文件不变。
- Publication：同目录 .partial → write → checked flush/close → atomic rename。
  标记 complete 也可存在于临时文件，不能单凭标记批准发布。没有 fsync、真实 ENOSPC 或断电持久性验收。
- Viewer 已有总览/缩放/平移/Fit/native Inspector；Native AMR 与 Displayed LOD 分开表达。
  LOD 是显示采样，不能替代原生值或科学积分。

build_id、effective_config_sha256、source_git_head 仍 unknown；生产文件没有这些逐键 reason 属性，
adapter 应保留该缺口，不能从当前 Git HEAD、路径或文件名伪造 known。
EOS 的 resolved 记录与独立 EOS 科学认证不同。外部可信期望需来自受控运行/输入/构建记录。
既有 evolved-reader 摘要的 completion=unknown/renderEligible=false 仍按原记录保留，
不能把低层读取成功当成 Viewer 已批准渲染或完整发布身份已核验。

查询仍有 whole-file digest 与叶单元扫描，没有跨查询空间索引/cache；
固定返回尺寸只限制返回数据量，首次全域总览可能扫描全部叶块。
原始 H5/plt/checkpoint、完整数组、ELF、日志全部留本机 ignored studio/.local。

下一步先由 owner 按此映射接读取适配层；按具体 finding 修改单位、基底、测度、身份或发布语义。
未冻结曲线坐标/3D/XDMF；二维演化、独立科学 oracle、全域一致性及大文件成本仍需各自证据。
