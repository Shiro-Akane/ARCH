# 验证拉取后的 ARCH

[English](README.md) · [构建指南](../docs/guides/Build.zh-CN.md) · [科学验证](../validation/README.zh-CN.md)

源码包含测试代码、小型独立参考数据、制造解、验证清单和网络生成配方。测试程序需要
本地编译；生成网络、外部库、大型模拟输出和分析器数据不作为预编译依赖随源码提供。
使用受 Git LFS 管理的 EOS 表之前，请运行 `git lfs pull`。

以下命令均从仓库根目录在 Linux 或 WSL2 中执行。工具测试、数值回归和完整程序短测
各有用途；科学误差与验收预算统一见 [Validation](../validation/README.zh-CN.md)。

完整测试套件的编译耗时与主机内存需求都高于只构建应用。
`BUILD_TESTING=ON` 注册适用的测试目标，默认 `all` 构建会编译这些额外的可执行程序。
启用 `ARCH_ENABLE_CUDA=ON` 后，CUDA 测试还会增加大量 NVCC 编译工作。
这里的压力主要是主机内存，不是显存；显存需求在实际运行设备测试时才需要另行衡量。
较大的生成网络和更多目标 GPU 架构会继续增加编译工作量。

两个 Release 预设在首次应用构建中保持 `BUILD_TESTING=OFF`。新用户可以先用
`cpu-release` 跑通应用和下方工具检查，再按需启用测试或选择 `cuda-release`。
即使已启用测试，`cmake --build <build-dir> --target ARCH` 仍只构建应用及其依赖，
不会编译独立测试。在 `.par` 中改为 CPU 运行，也不会消除 CUDA 构建的编译成本。

## 不需要 GPU 的工具检查

使用 Python 3.10 或更新版本，以及构建环境中的 Git/CMake。测试使用标准库和受控
输入，不需要 pynucastro、NumPy、SciPy 或 CUDA 设备。

```bash
python3 tools/audit_architecture.py .
python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

完整资源保护测试需要 Linux `/proc`、子进程管理支持和 Python 的 `os.pidfd_open`
接口。缺少这些功能的 Python 构建或内核可能导致对应测试跳过。请阅读汇总：
跳过不表示功能已经验证。

## 编译并运行 CPU 回归

先按[构建指南](../docs/guides/Build.zh-CN.md)准备依赖。默认启用 KLU 以测试 CPU
稀疏求解；本机尚未安装时，配置阶段可能下载 HighFive 和 SuiteSparse。

```bash
cmake -S . -B build-test-cpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DARCH_ENABLE_CUDA=OFF \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-test-cpu/bin"
cmake --build build-test-cpu --parallel 1
ctest --test-dir build-test-cpu -N
ctest --test-dir build-test-cpu --parallel 1 --output-on-failure
```

CTest 不会编译缺失的程序；上述默认构建包含已配置的测试目标。需要先检查重启时，
可只构建 `arch_checkpoint_compatibility`，再用 `-R '^checkpoint_compatibility$'`
选择它。该测试通过两种后端共用的读取器检查完整状态恢复，并拒绝不完整或不匹配输入。

## 加入 CUDA 和原生稀疏求解器

按 [CUDA 构建指南](../docs/guides/Build.zh-CN.md)准备 NVIDIA 驱动、工具链和 cuDSS。
库位于自定义目录时设置 `CUDSS_ROOT`，不要求安装到系统目录。同一构建若还要验证
CPU 稀疏燃烧，应保留 KLU，它不能由 cuDSS 替代。

```bash
cmake -S . -B build-test-cuda -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_CUDSS=ON \
  -DCMAKE_CUDA_ARCHITECTURES=native \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-test-cuda/bin"
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-test-cuda --parallel 1
ctest --test-dir build-test-cuda -N
ctest --test-dir build-test-cuda --parallel 1 --output-on-failure
```

`native` 面向配置时可见的 GPU；为其他设备编译时使用指南中的架构选项。编译并行度
可以调整，不改变数值方法。设备测试先串行执行，测清显存需求后再增加并发。

请检查配置输出和 CTest 清单。cuDSS 专项与生成网络测试仅在相应库和网络包启用时
出现；不包含这些条目的构建没有测试对应能力。复现代表性完整配置时，先按
[网络配方与环境说明](../validation/network/README.zh-CN.md)生成 audit31 和弱反应网络，
再选中相应包 ID 重新配置。pynucastro 只在生成时需要，ARCH 运行不依赖它。
audit150/audit200 属于单独的规模测试，不是起步要求。

## 完整程序与重启检查

ARCH 和检查点比较器必须来自同一构建：

```bash
python3 tools/smoke_cuda_amr_runtime.py \
  --arch build-test-cuda/bin/ARCH \
  --checkpoint-validator build-test-cuda/arch_cuda_single_level_validation
```

[短测指南](smoke/README.md)说明动态 AMR、曲线扩散与检查点续算路径。逐字段恢复和
继续演化使用[重启验证](../validation/restart/README.zh-CN.md)，覆盖 CPU/CUDA 四种
方向，并检查原始组分、ENUC、时间步控制器及输出阶段。运行器保留日志，输出目录必须
不存在或为空，不会静默覆盖另一轮数据。

共用运行器通过 C++ 比较器读取 HDF5。独立科学参考脚本可能额外需要 NumPy、SciPy、
mpmath 或 h5py，各 [Validation 模块](../validation/README.zh-CN.md)均列明要求。
只有插桩检查需要 Compute Sanitizer。

## 查找与扩展测试

- [host/](host/README.md)：CPU 契约、AMR/几何、检查点、EOS 与燃烧。
- [cuda/](cuda/README.md)：设备执行、两侧一致性、内存与事务生命周期。
- [tooling/](tooling/README.md)：源码审查、构建元数据、运行器、身份与资源保护。
- [math/](math/README.md)：主机与设备共用的数值测试。
- [fixtures/](fixtures/README.md)：独立参考数据和受控系统。
- [smoke/](smoke/README.md)：短时完整程序检查。

测试组由 [CMakeLists.txt](../CMakeLists.txt) 选择，目标声明及条件位于
[HostTests.cmake](../cmake/tests/HostTests.cmake) 和
[CudaTests.cmake](../cmake/tests/CudaTests.cmake)。多个测试使用相同参考
数据时，共用一份即可。生产算法放在 `src/` 中，参考结果则应独立于被测函数计算。
