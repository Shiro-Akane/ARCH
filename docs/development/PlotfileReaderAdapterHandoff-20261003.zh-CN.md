# Plotfile 读取适配对接：当前候选实现

## 精确引用

- Owner验证contract：23ff77c4f08419de2b3c5eadee214da2af25784e；已完整阅读，未merge。
- 当前writer/query/Viewer源码：e7181420d06f3eb95eff85061d347ed0f26eaec6。
- 分支：studio/compute-optim-integration；本地提交，尚未push，不能称为远端可获取。
- 新t=0 writer clean构建来源：ab23bad7e8bea93c3569618d1a6335312372cc30。
- CPU binary SHA：c294f0d1be009bdeca36174a00a28c6ff6ca48b11b499416f6556a2ecb6b4bec。
- 本文是小切片对接，不是完整联合目标或科学验收封箱。

## 实际字段与数组映射

| 语义 | HDF路径/属性 | 排列 |
| --- | --- | --- |
| 时间/维数/几何 | 根time / dim / geometry；time_unit=s（CGS记录） | 标量 |
| 原始场 | /Data/<field> | FP64，1D[B,Nx]，2D[B,Ny,Nx]；i最快 |
| 场声明 | dataset metadata_version=candidate-field-1；unit/centering/basis/meaning/unit_reason | cell；未知单位带原因 |
| 笛卡尔中心 | /Grid/x,y,z；coordinate_unit=cm，coordinate_basis=cartesian | FP64[N]；与field C-order展平一致 |
| 原生bounds | /NativeGrid/x1_lower,x1_upper；x2/x3同规则 | FP64[N]；inactive均0 |
| 原生measure | /NativeGrid/cell_measure | FP64[N]；GridMetrics::CellVolume |
| measure声明 | /NativeGrid measure_unit / measure_normalization | 1D cm/per_unit_transverse_area；2D cm^2/per_unit_transverse_length |
| level/Morton | /Grid/level,morton | [B]；文件block顺序，不能按Morton重排 |
| logical key | /NativeGrid/logical_x1,x2,x3 | uint32[B]，与level组合；仅文件内身份 |
| 来源证据 | /SourceIdentity attrs及species_names | candidate-identity-1，partial |

全局索引：1D b*Nx+i；2D (b*Ny+j)*Nx+i，无ghost。
适配到owner候选bounds：[B,Ny,Nx,2,2]，最后维度axis及lower/upper；
measure reshape为field shape。使用分散属性适配，不在生产writer复制候选JSON。

两个新真实t=0文件实际仅含DENS：
Sod[12,16]、CellularDet[20,16,16]，float64；
DENS unit=g/cm^3、meaning=mass_density、basis=scalar、centering=cell。
文件SHA、raw输入与构建身份见PlotfileFieldRealT0-20261003.Summary.json。

## Producer声明范围与科学review

声明来自现有PlotIO实际producer，不改变原场计算：
DENS rho；PRES/TEMP现有EOS+实际组分；ENER total_energy_density（erg/cm^3）；
VELX/Y/Z当前Cartesian velocity_component（cm/s）；
ENTR P/rho^Gamma1 proxy，unit unknown并解释local EOS exponent，不标热力学熵；
ENUC specific_burning_energy_rate（erg/g/s）；
VORT/DIVV共享VelocityDiagnostics（1/s）；species_mass_fraction（1）。
未知ExtraPlotScalarField保持unknown/原因，不从dataset名字猜单位。
逐文件枚举/Data；不能要求每个文件包含全部字段。
其他字段声明已有synthetic/scoped证据，尚无完整真实生产全字段验证。

1D rho*measure单位g/cm^2，2D为g/cm；不能称为三维质量。
低维normalization不能被reader隐式补成三维volume。
固定CGS单位来源统一为src/data/FieldUnits.h；Core API与writer共用。
这里是recorded candidate，维护者unit/basis科学review仍待。

## 来源身份与发布失败

case来自ConfigurationInput.case_id；raw_config_sha256为实际parser字节；
binary_sha256来自Linux/proc/self/exe，只覆盖main executable；
EOS/table/gamma/species从resolved runtime checkpoint provenance。
eos_unit_system=cgs；run/effective_config/build/source_git仍unknown，
reader不由filename或当前HEAD补齐。文件digest不是完整科学来源身份。
适配校验必须使用文件外可信输入、构建和EOS身份。

同目录.partial → write → flush → checked H5Fclose → atomic rename；
异常传播、保留旧正式文件、发布后才打印成功。没有fsync/断电持久性声明。
已测write/flush/close/rename失败、发布前SIGKILL；reader拒绝partial名称。
实际磁盘耗尽/硬件断电仍未验证；complete marker不能单独证明发布完成。
checkpoint格式/原始数值不改。

## 查询和界面

只读metadata/raw slice/global LOD/physical point/explicit viewport。
LOD为coordinate-overlap-weighted display mean，不是原生值/科学积分；
Inspector按原生bounds返回原始stored value及身份，gap/重叠多匹配明确失败。
同digest叶块outline最多128个，limited如实显示，不合成parent/coarse场。
zoom/pan/Fit只重绘；细化读取显式请求，保留full LOD与revision protection。
首次总览/viewport仍全叶扫描+digest；固定像素只限制返回量。
没有真正空间索引/cross-query cache，大文件和同机资源影响待测。
measure_plotfile_query.mjs的maxRSS含Node/WASM，rchar非纯HDF bytes；
缓存read_bytes=0不能解释为无逻辑扫描。

## 已验证与未关闭finding

- 309项Studio/Host回归、lint/typecheck/build通过。
- 新t=0 Sod192/Cellular5120个DENS与checkpoint位级差异0；
  center/measure最大差0，keys一致。
- Cellular per-cell bounds对Preview最大差1.7763568394002505e-15，
  保留finding交review，不自定容差。
- Linuxproduction native已验证Sod/Cellular新单位、轴、原生Inspector，
  两个实际点经独立h5py核对；见PlotfileNativeUnitUat-20261003.zh-CN.md/Summary。
- 导航缩放/平移/Fit证据另见PlotfileNativeDesktopFindings-20261003.zh-CN.md。
- 全字段/完整科学身份/全域AMR无遗漏重叠/全部native负向交互及大文件仍待。
- stale binary启动Build文案和registry轮询待排查，不能把旧binary当current。

复现工具：validation/io/verify_initial_plotfile.py、verify_plotfile_blocks.py、
verify_plotfile_reader.mjs、measure_plotfile_query.mjs。
原始H5/plt/checkpoint/ELF/full arrays/logs仅本机studio/.local。
适配方可先按上述精确源码SHA、字段及bounds/measure mapping对接，
按具体missing/invalid findings逐项收敛，再冻结科学语义。
