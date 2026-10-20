# Plotfile 实际运行 binary 指纹

日期2026-10-03；基线5a8bd9202276ce6d4c2b827ef304a86fa11a6d84。
状态：main executable 内容身份已接入，不是完整build/source provenance。

## 实现和边界

共享FileFingerprint新增running_executable_sha256：
Linux通过/proc/self/exe调用现有streaming SHA256，每进程首次调用计算并保留摘要，
不使用argv[0]、当前目录、launch路径或当前工作树HEAD。
实际PlotIO在构建SourceIdentity时取此摘要。
SourceIdentity/binary_sha256、binary_source=Linux /proc/self/exe、
binary_scope=main-executable-only明确记录。
Linux读失败异常传播，非Linux返回空unknown；本轮不做Windows适配。
writer对非空digest检查规范格式，root plot_identity_state仍unknown/partial。

运行中的executable由Linux内核保持原inode，即使launch路径被替换也不改其身份。
摘要是主程序文件内容，不覆盖运行时共享库、链接工具链、环境或完整输入依赖。
现有构建来源没有可靠嵌入记录；build_id/source_git_head继续unknown，
不能用当前Git或sidecar文件名补齐。
已记录的LTO link-dependency缺口没有因此消失，dependenciesComplete/freshness不提升。
本轮不生成新的build manifest、不伪造完整构建证明。

## 验证

实际PlotIO object与配置/Plotfile/checkpoint三个CPU scoped target编译通过；
CTest configuration_input/plotfile_publication/checkpoint_compatibility 3/3 PASS。
writer fixture回读binary SHA与测试进程真实指纹相同；raw/shape/checkpoint保持回归。
增scoped target依赖触发现有build-cpu CMake正常regeneration，未新建build tree。

独立非科学probe：在ignored目录复制测试executable，启动后确认READY digest
与Python hashlib全文件摘要一致；只rename其独立launch路径并放入不同内容，
通知进程继续，fresh file_sha256(/proc/self/exe)与缓存指纹均保持原digest。
replacement路径摘要不同；exit0，owned child /proc条目消失。
准确hash见同名Summary.json；复制ELF与日志只留本机，不上传。
没有替换production ARCH、运行simulation/Preview/CUDA，未触碰其他进程。
diff check PASS；Studio/Host本轮未改，不重复279项已通过检查。

## 剩余

reader/client/Inspector还未消费SourceIdentity，显示仍unknown。
完整effective配置、构建证据/字段单位来源与真实Sod/Cartesian2D AMR验证、
publication fault injection/owner review、全域/局部Viewer及I/O/RSS仍未完成。
不因主binary摘要已知就称科学身份完整。整体联合计划继续进行，未push/tag/main merge。
