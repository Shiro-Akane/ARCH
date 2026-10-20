# Plotfile 字段声明与低维测度增量

基线 34490fc7670e2a26f2cb453e5af28682d95c016d，分支 studio/compute-optim-integration。
本提交在现有共享 writer 增加 candidate 声明；不改变字段公式、FP64、数组排列、
EOS 查询、GridMetrics::CellVolume 或 checkpoint v6。完整联合目标仍未完成。
owner 对接 contract 为 23ff77c4f08419de2b3c5eadee214da2af25784e。

## 生产者与序列化边界

标准字段由 PlotIO.cpp 的实际提取路径附带声明；species 路径声明 mass fraction。
额外 PlotScalarField 当前没有生产者 metadata 接口，保持 unknown/reason，
包括新增引力场；不从 extra field 文件名猜物理含义。
HDF serializer 不按名称查 catalog：即使低层测试数据叫 DENS，未提供声明仍输出 unknown。
bad declaration 在创建临时文件之前失败，不覆盖已有正式文件。

共享固定 CGS unit strings 移到 src/data/FieldUnits.h；配置 API FieldUnit 委托该 helper，
保留原 API 行为和未知 key 结果。PlotIO 复用固定单位并补充其真实 writer 语义。
CGS 依据为当前配置 API UnitSystem 的明确 contract 和维护者 PlotfileValidationContract；
历史 UI review 的 IdealGas 自定单位备注不是当前 contract。
标签本身不转换数值；不声称这些声明已通过维护者科学 review。

## 实际 HDF 映射

/Data/<field> dataset attrs：
metadata_version=candidate-field-1、unit、centering=cell、basis、meaning；
unknown unit 必须含 unit_reason。原 dataset shape 和值保持不变。

| producer field | unit | meaning / basis |
| --- | --- | --- |
| DENS | g/cm^3 | mass_density / scalar |
| PRES | erg/cm^3 | pressure / scalar |
| TEMP | K | temperature / scalar |
| ENER | erg/cm^3 | total_energy_density / scalar |
| VELX/Y/Z | cm/s | velocity_component / Cartesian 为 cartesian；其他基底保持 unknown |
| ENUC | erg/g/s | specific_burning_energy_rate / scalar |
| VORT | 1/s | vorticity_magnitude / scalar |
| DIVV | 1/s | velocity_divergence / scalar |
| ENTR | unknown | pressure_density_gamma1_proxy / scalar，unit_reason 明确局部 Gamma1 和非热力学熵 |
| 实际输出的 species | 1 | species_mass_fraction / scalar |
| 未声明 extra field | unknown | unknown / unknown，producer declaration unavailable |

SourceIdentity.eos_unit_system 在实际 PlotIO 路径为 cgs；低层调用未声明则仍 unknown。
cgs producer 对应根 time_unit=s、Grid.coordinate_unit=cm、
Grid.coordinate_basis=cartesian（Grid/x,y,z 是 physical Cartesian centers）。
旧原始 H5 不改写，也不把这些新标签补进旧文件冒充其既有证据。

Cartesian 1D/2D NativeGrid：
measure_unit 分别 cm/cm^2；
measure_normalization 分别 per_unit_transverse_area/per_unit_transverse_length。
producer 使用共享 GridMetrics 度量原值；serializer 拒绝与维数不一致的声明。
rho*measure 的总和分别 g/cm^2、g/cm，不能标三维总质量。
曲线和三维 native 候选支持范围未扩大。

## 已执行验证及范围

现有 build-cpu：CPU Release、CUDA OFF、source root 为本 worktree。
编译 publication test、受影响 PlotIO.cpp/API PresentationMetadata.cpp 对象；
未重链 production ARCH、未运行 simulation、未重新生成 t=0 参考输出。
新增测试后 publication 和 checkpoint_compatibility 两项通过；
write/flush/close 故障注入、rename/create 失败及不覆盖原文件沿 publication 测试复验。
坏单位原因、错误低维 normalization、原数组顺序和值、unknown extra、species 无量纲等有明确断言。

独立 h5py 对 C++ 测试生成的 1D [2,16]、2D [2,16,16] fixture 读回 metadata；
DENS dtype=float64，与已知 index+.25 数组一致。
处理摘要：PlotfileFieldDeclarations-20261003.Summary.json。
这是 synthetic serializer/producer-helper 检查，不是新的 Sod/Cellular 生产结果或全域 AMR 科学验收。
原有 non-square [2,3,5]、NaN/Inf 和 signed raw-value preservation 测试继续通过。
checkpoint 路径无改动，v6 identity/reader 回归通过。
没有 Studio frontend/Host 实现改动，未重复其此前已通过的305项回归或 native UAT。

## 未完成与 review

- 新声明的生产 ARCH 真实 t=0 输出、读取适配与 Viewer 单位消费尚待后续验证。
- run/effective config/build/source Git 身份仍 unknown；字段标签不解除完整 provenance 缺口。
- Cellular 单元 bounds 舍入 finding 保持未裁决，不改科学容差。
- 尚未做全字段原生一致性、全域 AMR 完整性、大文件/索引/缓存和同机运行影响验收。
- ENTR 的参数化单位表达、extra field 生产者声明及曲线基底留给后续独立 review。

原始 H5/plt/checkpoint 留在本机 ignored。仅提交源码、测试、声明说明和处理摘要；
不 push、不 tag、不 main merge。
