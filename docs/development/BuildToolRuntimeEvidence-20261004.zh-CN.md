# Build 工具 ELF runtime 的只读候选证据

当前成功 Host Build 已覆盖 generator、compiler、linker 输入，但未证明工具 runtime 完整性。
本次新增 validation/io/audit_build_tool_runtime.py，检查 Linux x86-64 ELF 的
PT_INTERP 与 DT_NEEDED；固定 readelf/ldconfig 仅作只读 inspector，
不执行被检查的工具、shell 载荷或 library。
本次没有 configure/build/simulation、生产 Manifest 修改、科学 flags 或 Core 变更。

## 实际来源与结果

从成功 Build e62f1ecc-7269-4b35-bad9-7172c6b68cfc 的 compilerDrivers/components
读取真实路径，加上固定 CMake/Ninja，共 12 个 root；
65 个唯一 resolved ELF 文件、209 条 loader/DT_NEEDED 边，unresolved=[]。
记录全部 path/realpath/size/SHA、loader cache SHA、两个 inspector 身份及图关系；
各输入在局部读取前后及完成前复核不变。
这是静态 default-cache candidate graph，status=partial，dependenciesComplete=false。

初次实际运行错误地使用 /usr/lib/gcc/.../cc1plus，真实 Ubuntu 24.04 路径在
/usr/libexec/gcc/...；该失败保留，修正为从 Manifest 消费路径，没有更换工具链。
当前 Manifest 的 roots 包含实际 selected linker /usr/bin/ld.mold，不能用
/usr/bin/ld 的身份代替它。完整 root 列表及 SHA 见 Summary.json。

## 安全与验证

文件上限128MiB，readelf/ldconfig输出1MiB及10秒，root最多32、图最多256节点；
只读O_NOFOLLOW/O_NONBLOCK打开，拒绝非普通文件，FIFO不会阻塞。
不解析 shell，不执行输入 binary；不支持其他架构。
缺失/多候选库及 RPATH/RUNPATH 必须进入 unresolved，不能猜实际选用库。
依赖循环去重；预算超出失败。

初次测试5/6，directory exception预期不匹配；修正异常断言并补nonblocking FIFO保护。
最终8/8：ELF声明、架构/坏needed、cache歧义、禁止任意inspector、
不执行带touch载荷、symlink/content/size/FIFO、missing/ambiguous/RPATH、
cycle和node预算。原日志保留，不覆盖为绿色。
没有重复无变化的 Studio/Host 或科学 baseline。

## 不等价于完整 freshness

这些是候选静态依赖，不证明进程实际 loader selection。
未覆盖 dlopen/plugin、hwcaps/preload/environment、非ELF工具数据、完整CMake输入闭包、
across-Build稳定或生产Manifest接线。
readelf/ldconfig本身也可能依赖动态库，记录其binary SHA不是完整审计器闭包。
不能凭 unresolved=[] 或65文件就设置 dependenciesComplete=true。

下一步把需要的持久身份与真实加载/选择证据分开接入 Host；先覆盖 drift/legacy/损坏，
再验证真实Build前后与跨进程恢复。完整closure仍独立待证明。
科学阻断、CPU全套、JENS/RZ、CUDA/O9不因本工具变为PASS。

原始输出在 studio/.local/integration/build-tool-runtime-20261004；
只提交脚本、测试及处理后的身份图/摘要，没有原始H5/plt/checkpoint/ELF。
