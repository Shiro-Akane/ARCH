# 原始 Init probe 组分身份关联

## Finding 与修复

DiffusionMode 原生验收证实连续 field Preview 不输出 mass-fraction arrays：
src/api/preview/Preview.cpp 仅序列化6/7/8个流体/热力学字段。
已有 inspect-case 独立返回少量原始 Init probes，包含 massFractions；
其 state.species 来自 SpeciesSnapshot 的 Core index/name 顺序。
原 UI 只显示无名称数组，Host/client validator 也没有核对数量、registry index 或单位。

现在要求 registry index 连续且与实际数组位置相同、名称非空且唯一；
每个 probe 的 massFractions 数量必须与 registry 相同，unit必须为既有contract的1。
缺少身份、反转索引、重复index/name、长短不符和错误单位明确拒绝。
空 registry+空 composition 合法；不添加伪造 gas/default。
这些是存储身份/结构检查，不另设质量分数误差预算、归一化或物理上下限。

独立 InitProbe 组件逐行显示 species index/name/raw value/unit；
String(number) 保留 JS number 原始表示，-0 单独保留，不做归一化。
仍明确 sparse Init before EOS conversion，不当作 full field / cell average / AMR cell value。
Current/stale/revision/Build关联继续沿用现有 workflow envelope，不改变请求/保存行为。

## 验证

相关Host生命周期、契约反例和实际React SSR渲染22/22 PASS。
完整Studio/Host331/331 PASS、0skip；lint/typecheck/production build PASS。
原 large bundle warning保留，不做无关打包重构。
真实 CPU DiffusionMode inspect-case 的3probes通过新validator；
species index0 background/index1 tracer，原始结果包括
[.5,.5]、[.7499749010540354,.2500250989459646]、
[.5,.49999999999999994]。不重写/舍入Core原值。
SSR反例验证-0/name/index正确对应、原对象不变及空species提示。

Core ELF沿用clean64b0ce2f源、SHA d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75。
原Core代码/算法未修改或重新Build；没有simulation、科学输出或阈值变更。
生产原生桌面新增组分表尚未UAT，不能用SSR替代该项。
本机ignored .local保留真实响应和完整检查日志；只提交处理后的摘要和代码。
连续组分场需要真实Core Preview contract扩展，不由frontend插值probe或从文件名猜测。
科学CPU/Jeans/RZ/CUDA/O9及全模型桌面矩阵仍未完成，未push/tag。
