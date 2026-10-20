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
生产原生桌面验证已补齐，见下节；SSR与原生证据分别保留。
本机ignored .local保留真实响应和完整检查日志；只提交处理后的摘要和代码。
连续组分场需要真实Core Preview contract扩展，不由frontend插值probe或从文件名猜测。
科学CPU/Jeans/RZ/CUDA/O9及全模型桌面矩阵仍未完成，未push/tag。

## Linux native production UAT 补证

从提交65aed733f21702258bca17527f6c30922db04a73和刚生成production assets启动独立窗口19074204。
真实点击Inspect initialization→Current，展开Raw Init probes，三个probe均能滚动到组分表。
名称index0 background/index1 tracer正确；Probe0两项.5，Probe1原值
.7499749010540354/.2500250989459646，Probe2 tracer .49999999999999994原文可读。
解释文字明确no normalization/EOS conversion、not full composition field。
全程未编辑/Save配置，未启动Run；正常close launcher session33256 exit0，
8owned PID+startTicks全部退出，inputSHA不变/output不存在/managed clean。
本轮只追加UAT记录，无新代码；不重复刚通过的331项检查。
连续composition field及全项目科学出口仍未因此完成。
