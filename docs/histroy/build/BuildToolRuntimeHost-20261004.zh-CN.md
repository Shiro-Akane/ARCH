# 3C static tool runtime 进入 Host Manifest 与 freshness

## 行为

Host新增只读Node adapter，使用固定/usr/bin/readelf及/usr/sbin/ldconfig，
只读fingerprint和ELF检查；不执行根工具或库，不接受browser command/env。
记录generator+compiler+GNU subprocess/plugin/selected linker roots、PT_INTERP、
DT_NEEDED、库default-cache候选、loader cache与inspector的path/realpath/SHA/size。

BuildRunner在已有Build前后快照通道采集；makeManifest保存toolRuntime及
toolRuntimeStableDuringBuild，失败保存toolRuntimeError，不绕过Build失败或伪造证据。
读取缺失runtime的旧Manifest兼容，但freshness保持unknown。
磁盘loader拒绝坏SHA、重复节点、伪complete、非法路径/版本/预算。
已知持久库/工具/cache内容或目标变化标needs-build；即使新graph读取失败，
仍保留已确认的旧文件漂移，不能用unknown吞掉该证据。
不完整/不稳定runtime保持unknown；dependenciesComplete=false。
缺失、多候选、RPATH/RUNPATH明确unresolved，不猜实际加载库。

新增静态证据不能覆盖用户授权/资源/科学边界；没有Python生产依赖。
CLI的Python audit同步修复带空格库缓存路径，node与Python保留候选图语义。

## 实际验证

定向初次24/25，带空格fixture暴露cache正则遗漏；修复真实解析。
第二次24/25为RPATH负向fixture未复位，仅修fixture。
最终26/26通过，包含旧磁盘Manifest、坏hash/重复记录拒绝、
已知库内容漂移在新图失败时仍needs-build，不执行touch载荷。
原失败日志保留。

随后完整Studio/Host326/326、0skip，Python8/8、typecheck/lint、
production Vite build通过；原>500kB chunk提示保留，未重构。
26为完整326中的子集，不能累加为352项。

真实build-studio-cpu读取12 roots、65 ELF、209 edges、unresolved=0；
两个不变只读snapshot相等。Host/此前Python实际图的resolved path/SHA/size及边集合相同。
这是actual read与适配一致性，不是across-Build稳定、实际加载闭包或科学认证。
本轮未执行ARCH build/configure，没有重写既有成功Manifest。
下一步需在clean提交源上真实Build，确认前后稳定及新Node进程disk恢复。

## 未完成出口

静态default-cache candidate不证明实际loader选用。
dlopen/plugin、hwcaps/preload/env、非ELF工具数据、完整CMake输入、closure仍待。
固定inspector自身runtime也需独立覆盖；不以65个文件/unresolved=0置complete。
Profile dependenciesComplete=true时，static-only证据也不足以claimcurrent。
原科学阻断、全CPU退出条件、Jeans/RZ/CUDA/O9保持独立未完成。
原始log/实际图在ignored .local；提交源码、测试及处理后的摘要，不提交H5/plt/checkpoint/ELF。
