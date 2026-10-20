# 验证拉取后的 ARCH

[English](README.md) · [构建](../docs/guides/Build.zh-CN.md) · [科学验证](../validation/README.zh-CN.md)

工程测试检查数学叶、接口、生命周期和失败处理；科学验证补充独立参考与实际程序终点。不需要安装 FLASH 或其他模拟软件。旧证据只在其源码、输入、执行物及适用范围内复用，修改后运行受影响的原测试所有者。

## 选择最小有效检查

| 改动 | 入口 | 能证明什么 |
| --- | --- | --- |
| 验证／构建／报告工具或文档 | `python3 -B tests/tooling`，以及下方架构／链接审查 | 运行器、身份、资源和当前文档契约，不是物理精度 |
| CPU 数学、配置、API、AMR 或 IO | 既有 [Host](host/README.md)／API 目标及 CTest | 对应独立数学或接口断言 |
| CUDA 索引、launch、分配或驻留 | 既有 [Device](cuda/README.md) 目标及适用整程序短测 | 对应范围的真实设备执行，不能由 Host 结果代签 |
| Studio／Host | [Studio 检查](../studio/tests/README.md) | 类型响应、请求身份、进程生命周期、渲染及构建契约 |
| 科学终点、收敛或耦合演化 | 对应 [Validation](../validation/README.zh-CN.md) 清单／参考 | 原误差、守恒、拓扑和新进程续算要求 |

优先扩展既有断言所有者，不为每个参数新建程序或探针。独立参考不能调用生产算法制造共同错误。失败先用最小复现定位，不以更大矩阵掩盖尚未解释的失败。长程、sanitizer 和公平 CPU/GPU 成本对照不进入每次 PR。

## 不需要 GPU 的工具检查

使用 Python 3.10+、NumPy/h5py、Git 和 CMake。Linux 资源检查需要 `/proc`、pidfd 和子进程支持。本工作站科学 Python 使用已有 `work` 环境；资源套件使用提供 pidfd 的系统 Python。设施缺失或跳过不表示完整验证。

```bash
python3 tools/audit_architecture.py .
python3 tools/check_documentation.py .
python3 -B tests/tooling
```

完整 Tooling 入口拒绝空套件、失败和跳过，包含稀疏轨迹 transcript 控制。依赖科学 Python 的独立 oracle 检查仍归各 Validation 所有者。文档检查核对当前页面的路径／标题锚点，历史冻结材料不改写。

## 编译并运行 CPU 回归

先按[构建指南](../docs/guides/Build.zh-CN.md)准备依赖，必要表使用[离线身份核对](../docs/guides/Build.zh-CN.md#离线表与-lfs-流量)。默认预设关闭测试；`--target ARCH` 只构建应用及依赖。CTest 不编译缺失程序。

```bash
cmake -S . -B build-test-cpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DARCH_ENABLE_CUDA=OFF \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-test-cpu/bin"
cmake --build build-test-cpu --parallel 2
ctest --test-dir build-test-cpu --show-only=json-v1
ctest --test-dir build-test-cpu --parallel 1 --no-tests=error --output-on-failure
```

重型构建沿既有资源护栏，按实际进程树 RAM 选择并发；外层只有一个 job，dispatch／LTO 也可能占数 GiB。局部改动先构建原目标，用 `-R '^name$'` 选中其 CTest，再按风险扩大。

未筛选命令包含唯一 `tooling_contracts`。若已执行完整 Tooling，则发现和运行同时使用 `-LE '^tooling$'` 避免重复，其余配置测试仍全部保留；局部名称过滤不代表完整 CPU 覆盖。

Host 比较器 `arch_cuda_single_level_validation` 无需 CUDA；历史名称不构成设备依赖。`checkpoint_compatibility` 与 `checkpoint_temporal_comparison` 保留原完整恢复／拒绝和时间比较契约。

## 加入 CUDA 和原生稀疏求解器

使用独立构建树及 [CUDA 构建说明](../docs/guides/Build.zh-CN.md)。只有真实 cuDSS 库可用时才启用它，KLU 仍负责 CPU 稀疏路径。`native` 只对应配置时可见 GPU，不代表全平台资格。

```bash
cmake -S . -B build-test-cuda -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_CUDSS=ON \
  -DCMAKE_CUDA_ARCHITECTURES=native \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-test-cuda/bin"
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-test-cuda --parallel 2
ctest --test-dir build-test-cuda --parallel 1 --no-tests=error --output-on-failure
```

保留 CUDA 的 `RUN_SERIAL`、提供者条件、不可用设备退出码及编译池限制；跳过不能签收 GPU。生成包测试只在实际发现／配置包时出现；空选择表示全部可发现包，不是禁用。生成与独立弱反应／NSE 参考见[网络验证](../validation/network/README.zh-CN.md)。

## 完整程序与重启检查

ARCH 与比较器须来自同一构建。[短测](smoke/README.md)、[重启](../validation/restart/README.zh-CN.md)和[引力](../validation/gravity/README.zh-CN.md)分别负责有界应用、新进程及可选场检查。清单定义真实终点、输入、字段和门槛；按步诊断不能替代要求的同物理时间比较。

测量前冻结源码／配置，记录编译和运行墙钟、编译器／profile、线程、RAM／swap 与退出原因。纯注释复用适用科学证据。异常缓慢可由既有护栏停止并保留到达步数／时刻及证据，截断不能记 PASS。HDF 批次前检查实际宿主盘；本工作站约余 1 GiB 时预警，不能用 WSL 虚拟余量代替。

## GitHub 持续集成

[CI](../.github/workflows/README.md)执行一次 Tooling、适用的完整 CPU/KLU 与 Studio 任务，并由一个必需状态汇总。明确的文档差异可省略昂贵原生／Node 工作并记录 `NOT_APPLICABLE`；未知范围完整执行。inventory/JUnit 与 TAP 均拒绝空项、缺项、失败或跳过。本地通过不等于托管执行或科学发布资格。

## 查找与扩展测试

[Host](host/README.md)、[Device](cuda/README.md)、[Tooling](tooling/README.md)、[数学](math/README.md)、[夹具](fixtures/README.md)与[短测](smoke/README.md)说明原所有者。注册在 [HostTests.cmake](../cmake/tests/HostTests.cmake) 和 [CudaTests.cmake](../cmake/tests/CudaTests.cmake)。保持已验 RKL core／Tabular 分工和燃烧分组；共享装配或注册不能丢掉独立子例或放宽阈值。
