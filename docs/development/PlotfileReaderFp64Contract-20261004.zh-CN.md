# 候选 Plotfile Reader 的 FP64 契约修复

联合计划独立 Plotfile 小切片；Sod 1D + Cartesian CellularDet 2D AMR。
基线 6b291494d5de8168e50ed57718a0a3cee7c3bd4c，owner contract 23ff77c4f。

## Finding 与修复

检查发布拒绝流程未发现新的绕过；但候选 NativeGrid 只对原生 bounds/measure
强制 FP64，/Data 和 /Grid 中心仅检查 shape。FP32 场和整数场可进入候选读取。
新增反例在未改实现时真实失败：Missing expected rejection。

现在对存在已识别 candidateNativeGrid 的文件，在任何 payload/overview 读取前，
逐项检查全部 /Data/<field> 与 /Grid/x,y,z 的 HDF 类型为 float、size=8。
错误明确指出 Candidate native field/coordinate requires FP64。
仅读取类型 metadata，不转换、填充或修复数组。未知/非法 native schema 仍拒绝。
旧文件没有候选原生声明时保留原结构审计，不能由此获得候选 FP64 或发布认证。

反例覆盖 FP32 field、integer field、FP32 coordinate；
metadata、slice、overview、point 与 production isolated worker 全部拒绝。
错误读取前后文件字节不变。另有 legacy FP32 结构兼容正例。
复用 plotfile-overview / metadata 现有入口，不新增 CI 矩阵。

## 检查和真实兼容读回

- 定向 24/24；完整 Studio/Host 329/329，0 skip。
- lint、typecheck、production build、diff check PASS。
- 既有 chunk-size warning 保持，frontend production asset 未变化。
- 既有真实 Sod 9 / CellularDet 28 字段 metadata，经新 production isolated Reader 通过；
  各读末单元 DENS 和固定32像素总览，文件 SHA 前后与原 producer 记录相同。
- Sod index191 = .125；Cellular index5119 = 10000000.000000006。
  这些是读取兼容性证据，没有重复独立科学验证或生成新输出。
- overview 分别扫描192/5120个原生单元；固定响应像素不限制扫描量。

原 producer source 869ae3d3、binary f82bb7ff16c4acf54ae84970b9b403dce3d0370a468d241169519b3bfd1f6a44
保持原记录，不用当前 HEAD 替代。摘要见同名 Summary.json。
日志与原始文件留 studio/.local；本轮未改 Core、Build ARCH、simulation、
checkpoint、物理阈值、单位或原始FP64数组，也未 push/tag。

## 阶段清单

- [x] 候选 native 读取的原场/中心 FP64 类型强约束。
- [x] 错误精度反例、旧文件结构兼容、既有真实文件读取验证。
- [ ] 完整 source/build/effective-config identity，独立科学 review。
- [ ] 二维演化 AMR、大文件 spatial index/cache 与首次扫描资源成本。
- [ ] 真实 ENOSPC/fsync/断电持久性。
- [ ] 全模型原生桌面矩阵与 O7 科学待决项、CUDA、批准 O9。

本次局部验收不关闭这些工作，不宣称完成发布、科学值或整个联合计划已获认证。
