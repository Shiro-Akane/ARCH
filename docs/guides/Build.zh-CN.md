# 构建 ARCH

[English](Build.md) · [快速开始](../../README.zh-CN.md#构建)

第一次构建可以直接使用 README 中的 CPU 或 CUDA 命令。本指南解释这些命令的
作用，以及如何根据机器调整编译。所有 shell 示例都应在 Linux 或 WSL2 终端中，
从仓库根目录执行。

## 先配置，再编译

`cmake --preset cpu-release` 执行**配置**步骤：它会定位编译器和依赖项，将选项保存至 `build-cpu/CMakeCache.txt`，并生成相应的 Ninja 构建规则。这个步骤尚未编译 ARCH 可执行文件。预设提供常用选项，也可以通过 `cmake -S . -B build-cpu -G Ninja ...` 逐项指定。

`cmake --build build-cpu --target ARCH --parallel 1` 才会**编译并链接**应用及其依赖。CMake 会自动为你调用 Ninja，因此你不需要再执行独立的 `make` 步骤。当你修改源码后，只需重复这条构建命令即可，Ninja 会智能地仅重新编译受影响的对象和链接。若需要修改构建选项，请先重新执行配置步骤。当更换编译器或构建生成器时，请务必使用一个全新的构建目录。

项目在 [CMakePresets.json](../../CMakePresets.json) 中提供以下预设，将产物分开放置：

| 配置命令 | 构建目录 | 可执行文件 |
| --- | --- | --- |
| `cmake --preset cpu-release` | `build-cpu` | `build-cpu/bin/ARCH` |
| `cmake --preset cuda-release` | `build-cuda` | `build-cuda/bin/ARCH` |
| `cmake --preset cuda-debug` | `build-cuda-debug` | `build-cuda-debug/bin/ARCH` |

三个预设都使用 Ninja 与 OpenMP。Release 预设设置 `BUILD_TESTING=OFF`；
`cuda-debug` 则启用测试，供开发使用。两个 CUDA 预设均通过
`CMAKE_CUDA_ARCHITECTURES=native` 面向配置时可见的显卡，并设置
`ARCH_CUDA_HEAVY_COMPILE_JOBS=1`。预设只负责配置，实际编译仍使用下方明确的
构建与内存监控命令。运行 `cmake --list-presets` 可列出可用预设。

第一次模拟建议从 `cpu-release` 开始，需要 GPU 执行时再使用 `cuda-release`。
CUDA 预设启用 `ARCH_ENABLE_CUDA=ON`，相比纯 CPU 构建会显著增加编译时间：
除了宿主应用，NVCC 还要实例化、编译设备执行路径并完成相应链接。
这些编译工作消耗主机 CPU 和内存，更多空闲显存不能消除主机内存压力。
较大的生成网络、更多目标 GPU 架构会进一步增加工作量；实际耗时取决于所选目标、
编译缓存、并发和机器，并非固定需要某个时长。

`.par` 中的 `compute_backend` 是运行时选项。这里选择 `cpu`，不会把
`ARCH_ENABLE_CUDA=ON` 的构建变成纯 CPU，也不会跳过 CUDA 编译。
希望省去这部分构建工作时，应使用 CPU 预设。

产物分离通过 `ARCH_RUNTIME_OUTPUT_DIRECTORY` 实现，它指向各构建目录下 `bin/`
的绝对路径。不设置该选项时，项目默认把可执行文件写入源码树的 `bin/`。
需要覆盖预设中的编译器、CUDA 架构等选项时，在配置命令后追加 `-D名称=值` 即可。

## 工具与依赖

本指南中的命令使用 Ninja，请先安装。CPU 构建需要 C++20 编译器、
**CMake 3.22 或更高版本**，以及包含 C++ 和
HL 组件的 HDF5 开发库。默认还需要 OpenMP，也可以用 `ARCH_ENABLE_OPENMP=OFF`
关闭。仅构建 CPU 后端不需要 CUDA 工具链。

CUDA 构建还需要 **CMake 3.25.2 或更高版本**、包含 **NVCC 12.0 或更高版本**的
NVIDIA CUDA 工具链、该工具链支持的宿主编译器及兼容的 NVIDIA 驱动。本项目
参考构建使用 CMake 3.28、CUDA 12.3 和 GCC 12。CUDA C++20 的 CMake 支持见
[3.25.2 发布说明](https://cmake.org/cmake/help/latest/release/3.25.html)。

首次配置会通过 Git 获取 HighFive 2.9.0，因此需要网络。KLU 默认启用：CMake
优先使用已安装的 KLU，否则获取 SuiteSparse 7.13.0。已有本地依赖源码时，可以
用 CMake 的 FetchContent 源码覆盖选项指定位置。OpenMP、HDF5、HighFive 和 KLU
声明位于 [cmake/Dependencies.cmake](../../cmake/Dependencies.cmake)；CUDA 专用
求解库（包括 cuDSS）的发现仍由 [cmake/CudaBackend.cmake](../../cmake/CudaBackend.cmake)
通过 [FindCuDSS.cmake](../../cmake/FindCuDSS.cmake)等查找逻辑处理。
通过 Git LFS 管理的 EOS 资源需要先执行 `git lfs pull`，再使用相应表格。

构建监控和测试工具需要 **Python 3.10 或更高版本**，ARCH 可执行文件本身不依赖
Python 运行。生成反应网络使用独立的 Python 环境，配置方法见
[网络指南](../../validation/network/README.zh-CN.md)。

使用 WSL2 时，NVIDIA 驱动安装在 Windows，Linux CUDA Toolkit 安装在 WSL 内，
不要在 WSL 内安装 Linux 显示驱动。具体步骤见
[NVIDIA 的 WSL 安装指南](https://docs.nvidia.com/cuda/wsl-user-guide/index.html)。

## cuDSS 与稀疏燃烧

CUDA 构建中的 `ARCH_ENABLE_CUDSS=ON` 启用可选库发现。适配器使用 **cuDSS 0.8
API**，并检查运行时加载的库与编译头文件的主、次版本一致。没有找到库时，构建
不包含 CUDA 稀疏求解提供者，需要它的运行配置会明确报错。其他 CUDA 功能及
小系统的 DenseLU 燃烧不需要 cuDSS。

不要求以 root 身份安装。CMake 会搜索常规位置，并读取 `CUDSS_ROOT`、
`CUDSS_DIR` 环境提示。用户目录下的安装前缀只要包含 `include/cudss.h` 及
`lib/` 或 `lib64/`，就可以显式指定。例如，**确实已安装到以下位置后**：

```bash
cmake -S . -B build-cuda -DCUDSS_ROOT="$HOME/.local/cudss"
```

自动发现成功时无需设置此选项。配置输出会显示 `[DEP] cuDSS found:` 及选中的
库路径，或说明稀疏求解提供者不可用。`ARCH_ENABLE_CUDSS=OFF` 可主动关闭发现。
如果同一个可执行文件还要在 CPU 上运行稀疏燃烧，保留默认的
`ARCH_ENABLE_KLU=ON`：KLU 只用于 CPU，cuDSS 只用于 CUDA。所选网络与 EOS 的
执行组合也必须编入程序，找到库并不会自动添加自定义网络。

## 并行编译与内存

我们建议先使用快速开始中的默认设置：Ninja、`ARCH_CUDA_HEAVY_COMPILE_JOBS=1` 以及 `--parallel 1`。第一个选项限制了重型 CUDA／分发编译任务的并发池，而第二个选项则严格限制了总的并发构建任务数。Ninja 会同时强制执行这两种限制。请注意，其他的 CMake 生成器不会遵守这个项目专用的任务池设定。

CUDA 构建可以放在内存监控下执行：

```bash
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-cuda --target ARCH --parallel 1
```

当 Linux 可用内存低于 1.5 GiB、观测到的 swap 相比启动时增长超过 256 MiB，
或内存／I/O 持续停顿超过限值时，监控会停止它启动的构建。已有 swap 不必为零。
默认压力限值是内存 full-stall 达到 20%，或 I/O full-stall 达到 50%，连续持续
10 秒。这是 Linux PSI 的停顿观测，不是 Windows 磁盘占用百分比；采样监控也
不等于预留内存。

监控要求可读的 `/proc`、Linux 内存和 I/O PSI `full` 计数，以及可用的 Python
pidfd 和 Linux 子进程收养（child-subreaper）支持。启动前会检查这些能力。
如果 WSL／内核环境缺少支持，应先更新环境再使用这套监控流程。`Ctrl+C` 或
`SIGTERM` 会停止所启动的构建及其编译子进程。在 `--` 前加入
`--log build-cuda/build-memory.log` 可以保存输出与测量结果，日志路径须尚未存在。

中等硬件参考配置是 WSL2、i7-10700 级 CPU、**16 GB 系统内存**和 RTX 3060 Ti
级 **8 GB 显卡**，已完成**重型任务 2、总并发 4**的核心构建。完成初始配置后，
可以这样尝试：

```bash
cmake -S . -B build-cuda -DARCH_CUDA_HEAVY_COMPILE_JOBS=2
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-cuda --target ARCH --parallel 4
```

这是一组实测参考，不是最低要求或所有机器的最优值。
[核心构建记录](../../validation/backend/results/cold-core-first-law-20260907/release-909/README.zh-CN.md)
列出了当时的网络和工作量。用 `free -h` 检查 WSL 自身分配到的内存，不要把
Windows 的全部物理内存都算作 Linux 可用空间，并给 Windows 和其他程序留下
余量。大内存机器可以提高任务上限，再比较监控结果。编译并发不改变模拟的
数值参数，也不限制运行时可用显存。

## 优化、编译器与目标显卡

Release 构建保留了完整的 CPU 优化，并在工具链支持时启用 LTO／IPO。请确保为 `CMAKE_C_COMPILER` 与 `CMAKE_CXX_COMPILER` 选择相同主版本的 GCC，并将 `CMAKE_CUDA_HOST_COMPILER` 也指向那个相同的 C++ 编译器。你应该在一个全新构建目录进行首次配置时就设定好这些选项；任何本地编译的依赖也都必须使用兼容的 LTO 工具链。请注意，你不需要为了调整构建并发数而去关闭 LTO。最后，为了严格保持数值行为的一致性，我们共用的浮点契约会故意在所有受支持的工具链上关闭 fast-math 和浮点收缩。

CMake 找到 `ccache` 后会自动使用，包括 CUDA 编译。它有助于缩短重复构建；
所引用的冷构建测量关闭了编译缓存。修改被广泛包含的头文件仍可能触发较多
翻译单元重新编译。

`CMAKE_CUDA_ARCHITECTURES=native` 面向配置时可见的 GPU。为另一台机器或多个
GPU 架构编译时，可指定工具链支持的列表，例如
`-DCMAKE_CUDA_ARCHITECTURES="80;86;90"`；生成更多设备代码会增加编译工作量。
使用这个选项，不要另外注入 `-gencode`，因为 ARCH 启动检查使用同一份架构列表。
Release 的 CPU 代码还使用 `-march=native`，迁移到不同 CPU 架构时应重新编译，
不要直接假定本机优化的可执行文件通用。

## 按需构建测试

两个 Release 预设均使用 `BUILD_TESTING=OFF`，不把回归测试放入首次运行流程，
但不关闭模拟功能。`BUILD_TESTING=ON` 在配置阶段注册适用的测试目标，配置本身
不会编译这些程序；随后默认的 `all` 构建会编译额外的独立测试可执行文件，增加
耗时和主机内存压力。启用 CUDA 的测试构建，还会在 CUDA 应用之外增加 NVCC 工作。

即使已启用测试，`--target ARCH` 也只构建应用及其依赖，不会构建独立测试。
只需要定向检查时，可以直接选择对应测试目标。例如，在已有 CPU 构建中启用并
运行一个小型宿主测试：

```bash
cmake -S . -B build-cpu -DBUILD_TESTING=ON
cmake --build build-cpu --target arch_boundary_plan --parallel 1
ctest --test-dir build-cpu -R '^boundary_plan$' --output-on-failure
```

CTest 负责运行测试，不会自动编译缺失的测试程序。不指定 `--target ARCH` 时，
构建的是默认目标集合；启用测试后这个集合会更大。更多定向检查见
[Tests](../../tests/README.md)，科学算例及验收使用
[Validation](../../validation/README.zh-CN.md) 中各模块的流程。
