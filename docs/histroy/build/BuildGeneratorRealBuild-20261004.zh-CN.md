# 3C 真实 Host Build 的 generator 身份持久化

## 范围与实际输入

使用 clean 提交 8e5204628925cd2b531507dbc4174e2752826310，
现有固定 studio-cpu-release profile / build-studio-cpu，GNU Release、CUDA OFF，
parallelism=4，保持既有 LTO 及科学 flags。
没有独立 configure、simulation、Windows、push/tag 或 main merge。
主科学验证 build-cpu/bin/ARCH 不在本次修改范围。

通过真实 BuildRunner.start → cmake --build → makeManifest/saveManifest，
随后启动独立 Node 进程 initialize/loadManifest；没有直接伪造或改写 Manifest。

## 结果

首次 Build d827915d-c3e7-439e-92f1-55744b86b03d，53 个实际构建步骤成功，
82 个 CMake 配置输入及 CMake/Ninja 工具身份前后稳定；
742 个 compiler inputs，175 个 linker inputs，missing=0。
首次 compilerInputsStableDuringBuild=false 保留原样；
新进程正确恢复同一 build ID 和 generator 身份，freshness-unknown，没有升级为 current。

为确认更新后的稳定证据执行一次无改动 Build：
e62f1ecc-7269-4b35-bad9-7172c6b68cfc，Ninja no work to do。
配置输入、compiler inputs、compiler drivers、explicit tracked inputs 均前后稳定；
binary 的 SHA/size/mtime 完全不变。
新 Node 进程从磁盘恢复同一 ID、两份 generator 身份、175 个 linker inputs，missing=0，
changedInputs=[]，状态依旧 freshness-unknown：
“Tracked inputs match; full dependency coverage is unknown.”

最终 Host ELF：
079f08ef5bc0e9cadfc6e31dce4bbd0d671886297c7f2e23e44f9bc5aeaf3835，
7412240 bytes，路径 build-studio-cpu/bin/ARCH。
首次真实观察到的旧 ELF 为
2c53420cde6f1e01c7997b39999d6073f5496e0fe3278e84b36e976de6eea117，
不能用更早历史报告中的 ee3de6cf 替代本次 before 身份。

CMake /usr/bin/cmake：
1c5227af4edd22d8d689def545e18ee458260c0fd579eba2187967f38817e638；
Ninja /usr/bin/ninja：
5965527e09fe2b3787772aa4f711d6a36b393e7f2fcaa744a7a96c5a4ddf59cb。
两者 path/realpath/size/SHA 随成功 Manifest 持久化并由新进程读取。

## 资源与验收边界

两次均使用既有 run_memory_guarded，保留 2048MiB available、
最大 swap growth 256MiB 及 PSI pressure guard。
首次 guard elapsed 47.282s，observed peak_owned_rss 4092172KiB，
最低 available 18568344KiB；recheck elapsed 5.015s，peak_owned_rss 440536KiB。
两次观测 swap growth=0、guard_stopped=False；采样不是容量认证。

本次只补齐 generator 身份的真实构建/持久化出口。
dependenciesComplete=false，完整工具 runtime/shared-library/configuration closure 尚未证明。
没有重复运行上一提交已通过且未变化的 322 项 Studio/Host regression。
脚本断言真实 Build 成功、clean source 身份、generator 数量/跨进程相等、
稳定 recheck 的 binary 未变、missing linker inputs=0、changedInputs 为空和 truthful unknown。

原始 log、完整本地 Manifest/ELF 与执行脚本留本机 ignored .local；
提交处理后的 Summary.json，不提交科学数组。
CPU科学阻断、JENS/RZ科学决策、CUDA及O9仍独立待完成。
