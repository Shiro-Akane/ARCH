# Studio 环境要求：Linux／WSL 与 Windows

[操作指南](Studio.zh-CN.md) · [构建模块](../../cmake/README.md) · [桌面入口](../../studio/desktop/README.md)

**推荐使用 Linux**，也可采用带 WSLg 的 WSL2。原生 Windows11 x64 同样支持完整的 CPU／CUDA Studio 工作流，但适配较新，可能存在不稳定性与 bug。两侧共用 Host 与科学服务；普通 Core 编译不依赖 Node、npm 或 Electron，桌面构建不要求 CUDA。CUDA 构建与可信工具上下文见[原生 CUDA 指南](WindowsNativeCuda.zh-CN.md)。

| 要求 | Linux／WSL2 | 原生 Windows11 x64 |
| --- | --- | --- |
| Core 与构建工具 | 原生 C++20、CMake、Ninja、HDF5／OpenMP | GCC/UCRT64、同 ABI 依赖和平台辅助程序；[准备](WindowsNativeW0.zh-CN.md)／[编译](WindowsNativeCpu.zh-CN.md) |
| CUDA Core 选项 | NVCC 与兼容宿主依赖 | 独立 MSVC/NVCC 依赖及可信编译环境；[CUDA 准备](WindowsNativeCuda.zh-CN.md) |
| 源码桌面运行时 | Linux Node24+、npm、Electron | Windows 原生 x64 Node24+、npm、Electron |
| 窗口 | Linux 图形会话／WSLg 与下方系统库 | 原生 Windows 桌面 |
| 确认 Run／Restart | `xterm`、`flock` | 已配置的 Windows Terminal 与 supervisor／publisher |

各侧的 Node、Core、CMake 缓存及依赖必须对应同一系统。便携包携带相应系统的 Host Node 与 h5wasm，Core 及系统／工具依赖仍由使用者提供。两侧共同的操作和 `.par` 路径说明见[使用指南](Studio.zh-CN.md#linux-与-windows-的路径)。

## Linux／WSL2

请用普通 Linux 用户启动 GUI；管理员权限仅用于安装系统依赖。以下组件和 Ubuntu 配方用于 Linux，Windows 配方紧随其后。

## 组件清单

| 组件 | 用途与要求 |
| --- | --- |
| C++20、CMake 3.22+、Ninja、HDF5 开发库 | CPU Core 构建；OpenMP 默认开启；HighFive 与可选 KLU 按现有构建配置准备 |
| Linux Node 24+ 与配套 npm | 源码构建和 TypeScript Host；Ubuntu 系统仓库中的旧 Node 不一定符合要求 |
| Electron 44.9.1 | 当前锁定桌面运行时；CMake 明确准备该组件，不依赖 npm 安装钩子一定执行 |
| GTK3、NSS、GBM、ALSA、X11/XKB 等 Linux 共享库 | Electron 窗口运行；用 `ldd` 核对当前 binary 的实际依赖 |
| Linux 图形会话 / WSLg | 显示桌面窗口和独立终端；需要可用的 `DISPLAY`，不是服务器无头运行入口 |
| `/usr/bin/xterm`、`/usr/bin/flock` | Run/Restart 的独立终端及输出目录协调；不是后台作业或调度器 |
| Python 3.10+ | 开发验证工具；完整科学测试另需 h5py、NumPy；运行 Core 本身不需要 Python |

便携 Linux 包包含 Electron、用于 Host 的 Linux Node、h5wasm 与生产资源。科学 Core、系统图形库、WSLg 和运行终端仍由使用环境提供。发布包不携带开发测试或历史 UAT 文档。

## Ubuntu 24.04 参考安装

下面的包名对应 Ubuntu 24.04；其他发行版按其包名准备同一类依赖。包管理器会处理关联库，已有组件无需重复安装。

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends \
  build-essential cmake ninja-build libhdf5-dev curl xz-utils \
  libgtk-3-0t64 libnss3 libgbm1 libasound2t64 \
  libx11-xcb1 libxkbcommon0 xterm util-linux
```

Linux Node 24+ 可由已有的用户级 Node 管理器或 Node 官方 Linux x64 包安装。使用官方预编译包时，核对同一版本的 `SHASUMS256.txt`，将整个解压目录放入用户安装目录，保留 Node 自带 LICENSE。不要使用 Windows `node.exe` 构建 Linux Host。

本机已安装 Node 24.21.0 到 `$HOME/.local/opt/node-studio`，可直接设置：

```bash
export PATH="$HOME/.local/opt/node-studio/bin:$PATH"
node --version
npm --version
```

这一路径是用户级安装约定，不要求其他用户采用同一用户名或安装路径。已有 Node 24+ 可直接提供其绝对路径。配置中选择的 Node 不可用时明确报错，不改用旧版本。

## 一次构建 Core 和桌面

在 ARCH 根目录执行：

```bash
cmake --preset studio-cpu-release \
  -DARCH_STUDIO_NODE="$HOME/.local/opt/node-studio/bin/node"
cmake --build build-studio-cpu --parallel 2
build-studio-cpu/bin/arch-studio --project "$PWD" \
  --binary build-studio-cpu/bin/ARCH \
  --case Sod --config simulation/Sod/Sod.par
```

首次构建需要网络以准备锁定的 npm/Electron 和 Core 依赖。`studio-cpu-release` 的 `build-studio-cpu` 与 Host 受控构建配置相同，避免在别的缓存上隐式重绑定。生成的源码入口依赖当前 checkout。要制作便携包，再执行：

```bash
cmake --build build-studio-cpu --target arch-studio-package --parallel 2
studio/.local/desktop-release/arch-studio-bin-linux-x64/arch-studio \
  --project "$PWD" --binary build-studio-cpu/bin/ARCH \
  --case Sod --config simulation/Sod/Sod.par
```

包内 Host 优先使用随包 Node。需要显式替换时设置 `ARCH_STUDIO_NODE`；无效的显式设置不会静默回退。当前版本号仍为开发版 `0.0.0`，制作包不代表完成正式 release。

## 原生 Windows11 x64

先完成 [CPU 环境](WindowsNativeCpu.zh-CN.md)，再按[桌面构建指南](WindowsStudioDesktop.zh-CN.md)使用共用的 `arch-studio-assets`、`arch-studio-runtime`、`arch-studio-package` 目标。CMake 通过原生 Node 的 npm-cli.js 调用 npm，不依赖 npm.ps1。源码入口为 `arch-studio.cmd`，便携入口为 `arch-studio-bin.exe`。

启动前按所选构建设置 `ARCH_STUDIO_WINDOWS_TOOLS` 的可信工具和 DLL 路径。编辑与 Preview 不依赖终端；合格 Run／Restart 使用明确的 `terminal = wt.exe`。辅助程序选用对应构建的产物：CPU 路线配 GCC/UCRT64 HDF5，CUDA 路线配 MSVC HDF5／CUDA／cuDSS，并按 [CUDA 指南](WindowsNativeCuda.zh-CN.md)传入编译环境。不要混入无关 Anaconda、MSYS 或另一 ABI 的 DLL。Linux `.par` 可调整宿主路径后沿用，`/home`、`/mnt/c` 不是 Windows 盘符路径。

## 开发验证环境

以下 Linux 进程接口要求不描述原生 Windows supervisor；各平台的实测范围分别记录。

本机科学测试依赖已在 conda `work` 环境中。激活后再配置测试，或将它的 Python 绝对路径传给 CMake：

```bash
conda activate work
python -c "import h5py, numpy; print(h5py.__version__, numpy.__version__)"
cmake -S . -B build-release-cpu -DBUILD_TESTING=ON \
  -DARCH_ENABLE_CUDA=OFF \
  -DPython3_EXECUTABLE="$CONDA_PREFIX/bin/python"
cmake --build build-release-cpu --parallel 2
ctest --test-dir build-release-cpu --output-on-failure -j 2
```

其他机器不必建立名为 `work` 的环境；选用具有相同依赖的 Python 即可。缺少 h5py/NumPy 时可在自己的 conda 环境中安装这两个包，不能把此类测试未执行记成通过。

完整 Tooling 检查还要求 Linux Python 提供 `os.pidfd_open` 与 `signal.pidfd_send_signal`，用于确认只终止本次任务拥有的进程。本机 `work` 的 Python 3.12.14 未提供这两个接口，系统 `/usr/bin/python3` 提供。因此科学 CTest 使用 `work`，完整 Tooling 与资源保护器使用系统 Python；这两类环境不能只凭版本号互相替代。

开发者执行完整 Tooling 或 Studio／Host 测试时，Ubuntu 24.04 可补齐以下依赖。`mold`、`shellcheck` 用于对应工具与构建身份检查，不是桌面用户的必需组件。

```bash
sudo apt-get install --no-install-recommends \
  python3 python3-numpy python3-h5py mold shellcheck
/usr/bin/python3 -c 'import os, signal, h5py, numpy; assert hasattr(os, "pidfd_open") and hasattr(signal, "pidfd_send_signal")'
/usr/bin/python3 -m unittest discover -s tests/tooling -p 'test_*.py' -v
```

正式 CI 还检查测试集非空且没有 skipped 项；缺少进程接口或依赖时，应先补齐环境，不能将跳过的保护检查算作通过。调用 `tools/run_memory_guarded.py` 时也选用具备上述接口的系统 Python；被保护的科学命令仍可使用 `work` 的 Python。

## 启动检查

```bash
printf '%s\n' "$DISPLAY" "$WAYLAND_DISPLAY"
test -x /usr/bin/xterm
test -x /usr/bin/flock
ldd studio/node_modules/electron/dist/electron
```

`ldd` 中出现 `not found` 时，先补齐对应系统库再启动。`DISPLAY` 未配置时先恢复 Linux 图形会话或 WSLg。包内 Electron 路径改为发行目录的 `arch-studio-bin`。缺少 Core binary 时先完成上面的 CPU 构建；未注册的新源码需重新配置并编译，旧 binary 不能提供它的 Preview。

本机验收包括两条真实桌面启动路径、包外 HDF5 读取，以及真实 xterm 中的短运行和 checkpoint 续算。完整参数点击、原生选择器与全部显示组合仍须按[集成报告](../histroy/integration/ComputeStudioIntegrationReport-20261006.zh-CN.md)执行人工验收。
