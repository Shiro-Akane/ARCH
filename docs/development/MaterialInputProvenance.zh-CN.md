# 材料输入来源核对

本次证据基线：b523e0eef63a46bf6793f4470c9440a313ce41df。仅执行源码检查和 CPU binary 的 --list-cases，
未执行 Setup、EOS 求值或时间演化。14 个注册模型的 compiledSourceSha256
均与当前工作区实际 cpp 字节一致。下列身份是范围明确的来源证据，不代表
共享头文件、网络包、EOS 数据或整个 binary 的 Build Manifest 已全部验收。

## 登记路径及编译来源

| case ID | 所有者文件 | 实际路径 | cpp SHA-256 |
| --- | --- | --- | --- |
| BurnGradient | simulation/BurnGradient/BurnGradient.cpp | 网络登记 | 4171da69285643702258f2ae4758fc0519dba7cadda40804f3aefff09b5055c8 |
| BurnOneZone | simulation/BurnOneZone/BurnOneZone.cpp | 网络登记 | 52cb9928a8afcd8437d6c3b99149112bd310bbb9cbe47ea7d034bd6e41b503c3 |
| CellularDet | simulation/Cellular/Cellular.cpp | 网络登记 | afc2ae1c484def0d23e2f18c00da952cd886e1edf1883081bdc944874c4b9a01 |
| CooperativeHotspots | simulation/CooperativeHotspots/CooperativeHotspots.cpp | 网络登记 | dd6b0a29ee35da013ce712892a1c8de73e5c5e5124ff0f2efcbf950a3650698d |
| DiffusionMode | simulation/DiffusionMode/DiffusionMode.cpp | 模型直接登记 | b8e876946ba5d42e9110d4c80d672318a82c37b7120ea2a327e20c47cb19db77 |
| ExternalGravity | simulation/ExternalGravity/ExternalGravity.cpp | 无 species 登记 | 198e1be5635176864163fa27ea14dcaa383f10f4173f44fa23b2559e7eae4d71 |
| Gaussian | simulation/GaussianPulse/Gaussian.cpp | 网络或无网络材料分支 | f65735ace4bfc7b7e8e4ce69602b1527099c5e50147b5bf803dce592636f324d |
| GravityBox | simulation/GravityBox/GravityBox.cpp | 网络或无网络材料分支 | fd990ae21c9349b4e4d78d1de33dfdd94160d01cdf823af375be0c7e5b43cb59 |
| JeansWave | simulation/JeansWave/JeansWave.cpp | 无 species 登记 | 5f29312fa5cf0d729ee8be9bf99c89e10c71eabc8c8ef0255cbd730ddc44aa9b |
| RT | simulation/RTinstability/RT_instab.cpp | 模型直接登记 | 1bf4351f7508ad08780f4eed73f2722a7ae42ed4f32888be5205ecd3f69f2bba |
| SNIaCoupled | simulation/SNIaCoupled/SNIaCoupled.cpp | 网络登记 | 2fe391554874d95e3802f4f7b5d66565f3afd5cf488da4b2a8c14b4d80e58a6a |
| Sedov | simulation/Sedov/Sedov.cpp | 模型直接登记 | f9574da9e9ab762b55d7b128e28dfc7aadfed881c4bae838a2a6eaf07cc63be2 |
| SmoothAdvection | simulation/SmoothAdvection/SmoothAdvection.cpp | 无 species 登记 | addbe6ea60478e85ec3268740f17307f36c009c36a16b5bd40a9f2cf0c6e2210 |
| Sod | simulation/Sod/Sod.cpp | 模型直接登记 | f079c8272918dd02b39c3f3ad890abdb453523012a98edf94938fdf131337238 |

ExternalGravity、JeansWave、SmoothAdvection 的当前 Setup 不登记 species。
不能因材料来源整改而强制每个模型至少登记一种材料，或为它们补造伪组分。
注册模型、材料登记、EOS 转换和完整预览仍是不同能力。

## 直接模型材料的逐字段来源

下表数值逐项来自 add_species 实参，不根据相等数值推断来源。
A/Z/gamma_ref/Cv_ref 沿 Species.h 的既有字段定义，本次不改变数值或单位。

| case / 材料 | A / Z | gamma_ref | Cv_ref | 分类 |
| --- | --- | --- | --- | --- |
| Sod / SodGas | 1.0 / 1.0 | 已解析标准输入 gamma | 1.0 | 模型常数 + 输入 |
| Sedov / SedovGas | 1.0 / 1.0 | 已解析标准输入 gamma | 1.0 | 模型常数 + 输入 |
| DiffusionMode / background、tracer | 1.0 / 1.0 | 已解析标准输入 gamma | 717.5 | 模型常数 + 输入 |
| RT / LightFluid | 1.0 / 1.0 | 已解析标准输入 gamma | 717.5 | 模型常数 + 输入 |
| RT / HeavyFluid | 4.0 / 2.0 | 已解析标准输入 gamma | 717.5 | 模型常数 + 输入 |
| Gaussian / BgGas、PassiveGas，无网络分支 | 1.0 / 1.0 | 已解析标准输入 gamma | 已解析 case 输入 gas_cv | 模型常数 + 两个输入 |
| GravityBox / gas，无网络分支 | 1.0 / 0.0 | 已解析标准输入 gamma | 已解析 case 输入 gas_cv | 模型常数 + 两个输入 |

Gaussian/GravityBox 的 gas_cv 已由声明和加载负责存在性；Setup 中遗留的
Get 第二参数不是批准的缺失值来源。所选网络有 species 时，它们走网络路径，
不能同时声称使用上表的无网络材料。

## 网络材料

内置网络的名称、A、Z 分别来自所选 Derived 的 SPECIES_NAMES、AION、ZION。
TimmesNetworkSupport::RegisterSpecies 固定传入 gamma_ref=1.6667、Cv_ref=0.0。
这些是当前网络适配器的定义，不是用户参数或通用 EOS 常数。

新生成网络来自生成文件自己的 SPECIES_NAMES、AION、ZION，
GenerateNetwork.py 当前固定传入 gamma_ref=5.0/3.0、Cv_ref=0.0。
不得把它与内置的 1.6667 自动统一；这会改变既有数据。
生成器 SHA 不等于某个已生成网络包的 SHA，后续 provenance 须保留实际包身份。

| 共享所有者 | 当前 SHA-256 |
| --- | --- |
| src/physics/network/timmes_common/TimmesNetworkSupport.h | 5ede3ca28fd94a340e5f61819e21c4f9eb8dcdc01c1bd74a1cbe229218696314 |
| src/physics/network/aprox13/NetAprox13.h | 2dfbc8aececc68c71668248c463bf1f64bfd1e4570f872e37b0cfbdd0223d907 |
| src/physics/network/aprox19/NetAprox19.h | fcf9a77328b4102482aea707f24417381bfd86e881b30ea1ca1b5591b429462f |
| src/physics/network/aprox21/NetAprox21.h | d29983db5c69b6c246271c30fd312520cf32c24c5cea19ad251729c943df019b |
| src/physics/network/iso7/NetIso7.h | 530e75c35daa8e918e96a40ec5c6df81fe137b7f645271893d5d694a80527e73 |
| src/physics/network/InitialComposition.h | 41ea70b6be1346ff3e5d88b7ce17028802b52b89be3dae95be434ec6361a72a1 |
| src/physics/species/Species.h | d87878df904ba15b8c2b6e55afdb842be90a2f19a3fd43b35d3e07fe3f2a250c |
| tools/network/GenerateNetwork.py | fa37a2b3cb6d664b60eac435a2dfd090b16f4477609bb3105e7430b1fe9817ea |

初始组分读取已接到声明记录；其归一化保留原各自算法。
材料属性来源与归一化后的状态/修复来源应分开记录，不能因同为 species
就将固定核素属性、原始质量分数和修复结果合并成同一种 input source。

## 已完成与下一实现边界

- 已完成：实际注册清单、cpp 编译身份一致性、直接材料/网络/无材料路径、
  固定值与参数消费的逐字段核对。
- 尚未完成：在准备结果中逐材料保存上述属性来源，并在来源变化后校验；
  状态快照目前仅输出 species index/name，不应冒充完整来源显示。
- 后续接线：由登记所有者提供 ModelDefinition / ResolvedInput / NetworkTable
  证据，准备边界保留实际数值及来源；输入来源关联加载记录，模型来源关联
  注册 cpp 身份，网络来源关联真实网络包/构建输入身份。
- 必须拒绝遗漏/不一致来源，但不得为满足形式检查改写 A/Z/gamma/Cv 或新增
  species。纯数学单元的临时 SpeciesManager 与完整应用准备应保持边界。
- 本记录不宣称材料/EOS 科学验证完成；所有物理定义、输入缩放和误差门槛
  继续由维护者确认。整体配置、Studio、Jeans/RZ 交付仍未完成。

## 运行时接线进展

上述表格保留其明确的审计基线。随后实现 MaterialValue 和准备边界检查：
模型固定值保留其加载的注册源码身份，输入值引用解析记录的键，
网络表与网络适配器常数分别记录。登记数值被改写或来源遗漏时准备失败；
不登记species的既有模型仍合法。所有原数值保留，未修改归一化。

六个直接材料模型仅将原实参包装成来源值；在逐项确认数值/输入对应后，
更新CaseUnitEvidence绑定的cpp SHA。CPU binary的14个编译来源均匹配源码，
reviewed unit evidence均为current。此代码审阅不等于维护者的科学验收。

网络owner label不是实际网络包SHA或完整构建manifest的验证声明。
完整JSON/UI来源显示、网络包身份以及更下层直接C++入口仍有后续工作。
