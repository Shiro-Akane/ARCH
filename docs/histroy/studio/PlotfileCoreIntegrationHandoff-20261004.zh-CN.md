# Plotfile Core 读取适配层：当前精确交付

已完整读取 owner commit 23ff77c4f08419de2b3c5eadee214da2af25784e 的 PlotfileValidationContract.zh-CN.md 和实际验证器。未 merge owner 分支，不改变其判定规则。

累积实现 commit：5cd1e9085477c9b06e8fda5c7e8a623028e94ae1。本地 branch：studio/compute-optim-integration。这些本地提交尚未 push，不声称 Core 已可获取。工作树存在三个 validation/gravity/curved 的未提交 benchmark 工具改动；不属于本次 Plotfile 交付，也不纳入该 commit 的身份。

| 实现路径 | 最近修改 commit |
| --- | --- |
| src/io/plot/PlotIO.cpp | b53fdc93ddec32cfd144d12bd860a2e994555f41 |
| src/driver/io/DriverIO.cpp | b53fdc93ddec32cfd144d12bd860a2e994555f41 |
| studio/host/plotfileMetadata.ts | 5656f7b87abd075e464fbd48e1b98a82756949ba |
| studio/src/components/PlotfileNativeView.tsx | e7181420d06f3eb95eff85061d347ed0f26eaec6 |
| studio/src/components/PlotfileNativeInspector.tsx | 33a49399cb5f1f671a9de3d575fc4137c989b271 |
| validation/io/export_plotfile_handoff.py | 01371b1e4f0dc5e9b9fad57a73cd154fb7a3339f |

## 适配映射

/Data/<field> 为原生 FP64，1D [B,Nx]，2D [B,Ny,Nx]；只有活动叶块，无 ghost，i 最快。块按文件存储次序，不能先按 Morton 排序再索引场。
/Grid/x,y,z 中心按相同 C-order 展平；/Grid/level,morton 为 B 长度。
/NativeGrid/x1_lower,x1_upper 以及 x2 对应数组同序展平；cell_measure 来自 GridMetrics::CellVolume。
/NativeGrid/logical_x1,x2,x3 为块逻辑身份。
Adapter 可组合 bounds 为 [B,Nx,1,2] 或 [B,Ny,Nx,2,2]，末轴为 axis/lower-upper；measure reshape 为 field shape。不需要 writer 为验证器再复制一份 JSON HDF 布局。

dataset 属性 unit/basis/centering/meaning/unit_reason 原样交给适配层。DENS g/cm^3，PRES/ENER erg/cm^3，TEMP K，Cartesian VELX/Y/Z cm/s，ENUC erg/g/s，VORT/DIVV 1/s，species fraction 1。字段按文件实际枚举；ENTR 为压力密度代理量，unit unknown，有原因，不是热力学比熵。

1D 测度 cm、每单位横截面积；2D cm^2、每单位横向长度。密度积分分别 g/cm^2 和 g/cm，不称三维总质量。

## 来源、发布与读回

/SourceIdentity 分散属性记录实际 case、原始 config SHA、运行 binary SHA、EOS 和 output-session UUID。build_id/effective_config_sha256/source_git_head 保持 unknown，并由当前 writer 提供原因。文件自述与外部受控身份核对不同；不能以当前 Git HEAD、路径名或 parser 默认值补成 known。

历史样本形状、每字段声明和单点摘要见 PlotfileAdapterDelivery-20261004.json；该文件的 implementationCommit/currentMainBinarySha256 是历史记录，不是当前代码或 binary。历史 producer source/binary 保持原值。当前 CPU 构建与发布 fixture 见 PlotfileCurrentCpuBuild-20261004，source b53fdc93ddec32cfd144d12bd860a2e994555f41，CPU ARCH SHA 3d9c64c30f2d3bb18efad143be2d123cde9da37ecfb58cb99bc5cff2c640ee0d。不回填到旧 H5。

已有工程证据包含原生单元 FP64 位模式只读读回、全域覆盖反例、字段/checkpoint 一致性，以及 Driver write/flush/close/rename/create 故障传播。最新空叶网格修复拒绝发布且不消耗序号。正式发布为同目录 partial、checked flush/close、atomic rename；complete 属性不能单独证明成功。真实 ENOSPC、fsync 与断电持久性尚未验收。

## Viewer 与剩余边界

Viewer 已有 Native AMR 与 Displayed LOD 区分、原生 Inspector、总览和 Zoom/Fit。按钮 Zoom/Fit 有 Linux native UAT；原生 wheel/pan 完整物理映射仍未闭合，不据此宣称所有交互已通过。
Inspector 读原始叶单元 FP64；不以 LOD/colorbar 值代替，不改数组、单位或 checkpoint。
固定像素数只限制响应量，首次总览仍可能扫描全部叶块和文件摘要；JS heap 预算不是 HDF/WASM/RSS 硬保护。

下一步 Core 按上述分散属性和数组映射接入 adapter，运行原有顺序/精度/发布/身份反例；具体 finding 再做限定修复。独立 EOS/scientific oracle、二维演化和大型真实 AMR 成本仍单独待验收。首版不扩展曲线坐标、3D、XDMF。

本轮只核对 Git、文档、验证器和已有证据并生成此清单；没有新 Build、Preview、simulation 或重复 baseline。原始 H5/plt/checkpoint、完整数组及日志继续留在本机。伴随 JSON 给出准确代码 SHA-256，便于对方核对候选版本。
