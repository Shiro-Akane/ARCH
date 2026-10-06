# Sod 演化输出的隔离 Reader / client 补证

## 身份与范围

代码基线32c7d7e6b7dbf1c825166cc5554533b757cf2fbf。
数据来自此前固定输入的CPU Sod连续运行与0.05/step67续算，
终点0.2/step280；没有重跑simulation或改变科学/生产代码。
实际binary编译源码869ae3d3，SHA
f82bb7ff16c4acf54ae84970b9b403dce3d0370a468d241169519b3bfd1f6a44。

连续输出5份（time 0、.05、.1、.15000000000000002、.2），续算3份（.1、.15000000000000002、.2）。
时间保留实际FP64值，不把显示四舍五入的.15写成原始时间。
每份8 blocks×16 cells，DENS/PRES/VELX/ENER均FP64。
准确file/config/binary/session指纹见同名Summary.json。

## 独立参考和真实调用

build_evolved_plotfile_readback.py使用h5py从原HDF直接读取4个存储索引
8/63/64/119的中心、bounds、测度、level、logical key与四个字段，
以little-endian FP64 hex保存到本机ignored oracle。
这些点按原存储block顺序定位，不排序Morton，不用LOD值代替原值。

verify_evolved_plotfile_reader.mjs使用现有生产
inspectPlotfileMetadataIsolated/readPlotfileOverviewIsolated/readPlotfilePointIsolated，
每次均由Host-owned独立worker读取，随后通过当前client validators。
没有HTTP endpoint或native UI操作，也不绕过响应结构/身份校验。

## 结果

- 8份metadata和8份DENS总览通过Host与client校验。
- 32个原生点、128次字段查询全部通过；
  raw value、坐标、lower/upper bounds、cell measure与独立h5py FP64原始字节一致。
- block/start/linear index、level、file-local logical key一致。
- case/input/binary/run身份匹配外部受控记录；
  buildId/effectiveConfigSha256保持null，没有伪造freshness。
- cm、g/cm³等单位与原声明匹配；1D测度cm / per_unit_transverse_area保持。
- 8份文件SHA前后未变，source/continuation仍为两个独立output-session。
- 固定32像素总览仍扫描全部128个叶单元；没有缓存/索引性能承诺。

原始文件/完整数组/oracle保留ignored studio/.local/integration/current-cpu-sod-restart-20261004。
本提交仅两个复现工具、处理后指标与文档；相关diff check通过。
生产源码没有变化，故不重复运行已通过且无关联改动的Studio/Core全baseline。

## 限制与下一步

所有检查属于工程readback，不是独立物理oracle；
completion=unknown/renderEligible=false是现有审计响应语义，未因此升级。
不覆盖全域点枚举、二维演化AMR、native desktop UAT、大文件同机负载、
RZ、CUDA或O9长期物理终点。下一步继续owner适配/科学语义review及尚缺的阶段证据，
不以这组Sod验证宣称完整联合目标已完成。
