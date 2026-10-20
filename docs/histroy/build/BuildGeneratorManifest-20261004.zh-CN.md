# 3C Host generator 身份接入 Manifest 与 freshness

## 行为

CMakeConfigurationEvidence增加可选generatorTools，Host readBuildConfigurationInputs实际采集
固定/usr/bin/cmake与cache选中的Ninja path/realpath/size/SHA。
只读cache和binary；不执行cache命令。要求Host Ninja/source/build正确绑定，
cache2MiB、tool128MiB，检查读前后及symlink目标。
浏览器不能提交generator程序/参数/环境或修改fixed profile。

makeManifest已有configurationBefore/after通道现在同时覆盖两份工具身份，
sameConfigurationInputs不把缺generator证据的旧snapshot视为stable。
refreshFreshness：
- 旧Manifest缺generatorTools：configurationUnknown，不能current；
- 持久工具path/realpath/SHA/size变化：changedInputs含实际工具，needs-build；
- 无法读取或证据不稳定：unknown，不claimcurrent。
disk Manifest loader校验两条唯一角色、绝对路径、SHA/size，
拒绝损坏或重复generator records。optional字段保持旧Manifest可加载，
但缺项不能升级成新契约通过。

没有开启dependenciesComplete，也没有修改BuildProfile/科学Core/编译flags/旧Manifest。

## 实际验证

相关CMake/Build套件初次21/22，原CMake-only fixture缺新必需generator cache fields，
仅迁移fixture，原日志保留；新增fake executable含touch payload证明只读不执行，
工具内容变更→needs-build、legacy→unknown、corrupt/duplicate→loader拒绝。
定向22/22通过，随后完整Studio/Host322/322、lint/typecheck、production Vite build通过。
不能把22与322相加；npm test已含Host。
production仍有既有>500kB chunk提示，不顺手重构或当作性能认证。

真实build-studio-cpu读取82 CMake inputs、两份generator SHA；
legacy comparison false，两个不变只读snapshot comparison true。
CMake SHA1c5227af4edd22d8d689def545e18ee458260c0fd579eba2187967f38817e638，
Ninja SHA5965527e09fe2b3787772aa4f711d6a36b393e7f2fcaa744a7a96c5a4ddf59cb。
这只是actual read/identity，不是构建前后stable证明。
本轮未运行真实ARCH build/configure，没保存或重写生产旧Manifest。

## 未完成出口

下一步在clean提交源码上走真实BuildRunner，确认两份generator随成功Build持久化、
前后稳定以及新Host进程从disk重新读取。完整工具runtime依赖/全closure尚未证明，
dependenciesComplete=false/freshness-unknown保持。
CPU科学阻断与JENS/RZ/CUDA/O9验收不因这次Host工程接线清除。

source fingerprints、真实read与完整测试计数见同名Summary.json。
所有完整logs/本地读back脚本保留studio/.local/integration/build-generator-manifest-20261004。
没有simulation、新H5/checkpoint、Windows、push/tag。
