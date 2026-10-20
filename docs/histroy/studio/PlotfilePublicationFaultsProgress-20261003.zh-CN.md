# Plotfile 发布失败与中断验证

日期2026-10-03；基线f309d8f07379dd33f5a4b7b66aa4ac454abe4c85。
仅补测试与reader temporary拒绝，不改production writer或物理算法。

## 注入范围

Linux arch_plotfile_publication测试target通过linker --wrap包装
H5Dwrite/H5Fflush/H5Fclose，故障变量和暂停入口只编入测试executable。
production ARCH没有这些link选项/测试入口/环境开关。
逐项实际负返回值触发writer错误路径，并确认每种故障恰好命中一次。
异常到调用方、无Saved PLT、原final文件SHA一致、
旧raw内容仍可读、失败临时文件清理通过。
已有create/rename自然失败与successful replacement检查继续通过。
这些是工程故障注入，不是磁盘满/断电/hardware故障的实测。

## 中断

独立ignored目录复制一份已发布fixture，记录digest；
owned测试child在真正writer明确flush之前暂停，确认PID/start/握手与唯一partial，
原final digest仍相同后SIGKILL该child。
exit=-9，/proc child消失，无Saved日志，旧final字节完全保留；
一个partial留本机用于回查，不上传。
reader新增对writer .partial-XXXXXX保留名称的打开前拒绝，
实际中断文件和复制可读legacy H5的同名文件都被拒绝，
不依据temp内可能提前写入的complete属性伪装发布结果。

停止时点覆盖before-flush；没有冒称覆盖close后/rename前每个微小窗口、
断电耐久性或所有平台原子语义。没有fsync保证；遗留partial不自动删除。
当前candidate认证/rootunknown仍保持，不以文件名拒绝规则替代科学provenance验证。

## 结果

CPU scoped测试target增量编译与CTest plotfile_publication1/1 PASS。
测试依赖触发现有build-cpu正常CMake regeneration，未新建build tree。
npm283/283（包含Host测试）PASS，lint/typecheck/diff PASS。
frontend没改，不重复build；沿用上一增量已通过production build。
summary记录准确owned PID/start/旧digest与回收结果。
raw/log/复制fixture本机ignored，提交处理摘要。

## 剩余

真实Sod/Cartesian2D AMR逐值/坐标/单位/测度与身份review仍待完成；
Viewer全域/局部、LOD/轮廓/实际I/O/RSS与原生UAT尚未交付。
首次全域仍可能扫描大量leaf blocks，fixed image budget仅约束返回量。
整体联合任务未完成，未运行simulation/CUDA、修改production binary、
push/tag/main merge或Windows适配。
