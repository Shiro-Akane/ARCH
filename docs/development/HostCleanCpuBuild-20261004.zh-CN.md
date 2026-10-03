# 独立 clean Linux worktree：Host CPU 从零 Configure/Build

## 来源与隔离

从clean提交64b0ce2f8d97553f59024978618f1e4848a974e1创建detached worktree：
/home/arch/projects/ARCH-host-clean-build-20261004。
事先确认目录不存在、build-studio-cpu不存在、node_modules不存在。
使用固定Host ConfigureRunner/BuildRunner和已声明Node24.21.0，
未复制cache/object/ELF或node_modules；Host仅需Node内置模块，因此本次未安装frontend依赖。

profile=studio-cpu-release，GNU Release/CUDA OFF/OpenMP ON、parallelism=4，
保持既有IPO/LTO/科学flags与LTO输入保留。
原开发worktree与build-cpu/build-studio-cpu的binary SHA前后相同；
两个source tree均clean。没有simulation、新H5/plt/checkpoint、Windows或push/tag。

## 实际结果

Configure成功：d2ffa869-a885-4b3a-957b-22c9d682e461。
实际完成70个compile/link步骤，首次binary此前不存在。
Build：92d429da-42d9-4336-b956-79c43440b5c3，exit0；
ELF：d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75，
7412464 bytes。

158个CMake inputs、742 compiler inputs、175 linker inputs、missing0；
generator/compiler-driver/static-runtime/explicit inputs前后稳定。
static runtime为12roots/65ELF/209edges/unresolved0，
独立新Node从磁盘恢复同一build ID及runtime图SHA：
bdf69d6698d8f1d42b8b82c2ee7d8a21f64dd9698861b93658ae8e533e731e07。

首次没有pre-Build Ninja compiler dependency graph，因此
compilerInputsStableDuringBuild=false，保留freshness-unknown及实际原因。
不能凭successful fresh Build伪造旧graph或改写为true。
完整loader选择/dlopen/非ELF工具数据/CMake闭包仍未证明，dependenciesComplete=false。

新产物静态API smoke全部exit0：
--config-schema：94个parameters、1个auxiliary；
--list-cases：14个真实注册模型；
--preview-capabilities：真实协议可读取。
这些数量只描述本次binary，不作为生产常量或full-field/AMR能力推断。
未执行Setup/Init，未新增科学或UI验收。

guard整个Configure/Build/reload elapsed64.336s、最低available18799216KiB、
peak_owned_rss3656284KiB、观测swap growth0、无guard stop。
Build Manifest时间为21:08:12.544Z→21:09:02.741Z，约50.197s；
guard总耗时不冒称单纯compile耗时，更不是冻结科学终点benchmark。

## 验收边界与后续

这是独立源/新树上的真实Host clean-from-scratch CPU build证据，
与此前增量/no-op清楚区分；它补齐工程构建出口，但不清除完整CPU CTest科学阻断。
固定Host profile BUILD_TESTING=OFF，本轮没有完整Core CTest，不把API smoke当测试套件。
此前326项Studio/Host与Python8项未改动，因此不重复运行。
后续需实际loader闭包、全模型desktop验收及Core批准的Jeans/RZ科学方案；
CPU科学出口未通过前不提前认证CUDA/O9。

新worktree/build/rawManifest/ELF/log留本机持久目录供回查；
本报告和Summary提交在原集成分支，不在audit detached tree增加提交。
