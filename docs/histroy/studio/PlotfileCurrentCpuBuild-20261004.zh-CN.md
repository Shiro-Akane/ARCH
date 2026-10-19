# Plotfile 空网格修复：当前 clean CPU 构建复验

本次 source checkpoint b53fdc93ddec32cfd144d12bd860a2e994555f41，working tree clean。
既有 build-cpu CMAKE_HOME_DIRECTORY 指向本 source root；Release/Ninja/CUDA OFF。
通过标准 cmake --build 的 ARCH 与 arch_plotfile_publication 目标执行，parallel=28，
使用既有内存/压力 guard；没有独立 cmake -S/-B、clean 或 CUDA configure。
实际增量8步包含 CartesianPoisson.cpp、CompositeMultigrid.cpp、HDF5Writer.cpp、
PlotIO.cpp、DriverIO.cpp 及 gravity/dispatch archives、ARCH link。
前两者是已有待编译输入，不将 IO scoped suite 视为它们的科学验收。

ARCH SHA 从7d0360de4ac9a3429d2a8ffec3908ca4424f2c7716096f2c44ad189662a9d8c4
变为3d9c64c30f2d3bb18efad143be2d123cde9da37ecfb58cb99bc5cff2c640ee0d。
build exit0；guard elapsed11.195s、minimum available21917744KiB、
peak owned RSS2654264KiB、swap增长0、未触发guard。
这些是采样工程指标，不是物理终点benchmark或内存容量证明。

既有 CTest plotfile_publication 1/1 PASS，完整stdout/JUnit留本机。
在此当前CPU object set再运行直接 writer/真实Driver fixture，exit0：
空网格明确拒绝/no-dir/不消耗序号；后续有效网格使用同序号；
write/flush/close/rename/create失败传播、旧文件不变、重试与partial清理全部PASS。
fixture HEAD为上述checkpoint、dirty=false，当前两个IO源文件SHA和验证记录一致，
补齐上一轮复用旧dispatch object的边界。该 IO fixture 始终 time=0 / step=0；
没有运行 simulation timestep 或重新生成科学轨迹。
新增fixture H5与ELF只留ignored本机目录，提交处理后摘要。

原 Studio 受管 detached clean project 的binary仍是 d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75；
未将此更新默默替换到其 Build Manifest、Preview Session 或已有UAT身份。
现有Build证据已经采集 CMake/compiler/linker/static-tool-runtime，完整动态loader闭包
等仍未证明，因此 dependenciesComplete=false / freshness-unknown 继续保留。
不能从Ninja成功或本次link生成current全依赖证明。

没有不相关Studio源码变化，不重复334项、完整71项历史科学baseline或全模型UAT。
旧69/71失败、架构迁移待确认、Jeans/RZ科学待决、CUDA和O9仍未关闭。
原始H5/plt/checkpoint及完整日志不上传，不push/tag。
