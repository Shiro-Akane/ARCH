# Plotfile 加载时原始配置身份

日期2026-10-03；基线9a34e0dbf0777a2e1e9899148d34f8bbd79c8b67。
状态：raw config加载证据接入；不是完整运行provenance或Plotfile验收。

## 实现

ConfigParser已保留精确InputText，包括注释/空白/CRLF/末尾换行状态，
本轮没有改parser。RuntimeParams::Load/LoadText在成功case-aware加载时，
把该原文复制到ConfigurationInput.raw_text，raw_text_available=true。
分析对象默认false，不把partial inspection当成功加载。
配置保存为shared_ptr<const ConfigurationInput>，复制SimConfig沿用同一不可变证据。

PlotIO从LoadedInput原文调用共享arch::core::string_sha256，
不重新打开.par路径、不使用raw_tokens串接/归一化伪装原始字节。
SourceIdentity/raw_config_sha256与raw_config_source记录已捕获摘要和来源；
无捕获项unknown，effective_config_sha256继续unknown，不能将raw等同effective。
writer拒绝非空非法摘要，case/EOS源及partial范围保持上一版。
本轮持久配置额外保留一份原文，输出时hash该文本，不扫描仓库或文件系统；
未来需要测量时再决定是否在immutable load边界缓存摘要。

## 验证

configuration_input新增CRLF+注释+末尾无LF测试，精确原文相等，
共享SHA与独立Python hashlib固定参考一致。
仅加注释的两份输入parsed tokens相同，raw text不同。
真实文件Load后覆盖同一磁盘路径，LoadedInput原文与摘要仍为加载时版本；
测试文件位于独立mkstemp，成功后删除。
partial AnalyzeConfigurationInput未声称raw捕获。
IO manufactured fixture验证提供的raw SHA逐字存储，不冒称模型科学验证。
实际PlotIO/DriverIO object编译，配置/Plotfile/checkpoint scoped编译与CTest3/3 PASS。
新增测试依赖触发现有build-cpu正常CMake regeneration，不新建build tree。
diff check PASS。Studio/Host代码本轮未改，不重复279项不变回归。
原始日志/本机H5保留ignored路径，没有提交原始配置或完整数组。

## 待完成

running executable/build/source身份尚未接入，不能用当前Git HEAD或路径最新binary
替代实际运行binary；effective配置和field单位/定义来源仍pending。
reader/client尚未消费SourceIdentity，因此当前UI仍unknown，后续独立接线/测试。
真实Sod/Cartesian2D AMR输出与独立原生数值对照、publication failure tests、
科学owner review、全域/局部Viewer/LOD/I/O/RSS/native UAT尚未完成。
保持Linux/WSL；未运行simulation/CUDA、替换production ARCH、push/tag/main merge。
