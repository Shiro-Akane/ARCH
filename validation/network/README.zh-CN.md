# 反应网络验证

英文原文：[README.md](README.md)。英文版为规范文本。

反应网络给出燃烧使用的核素、反应率及耦合演化方程。这些测试从生成包的注册一直
检查到实际时间演化，并将弱反应引起的组分和能量变化与独立参考比较。稀疏案例
还实际检查大方程系统的求解路径，而不只是确认生成包能够编译。

本页结果对应[Validation 总索引](../README.zh-CN.md)注明的科学验收快照；源码组织与
构建检查见单独的[维护记录](../backend/results/maintenance-freeze-20260908/)。

CPU 和 CUDA 已通过生成网络的完整程序矩阵、真实 31 核素稀疏轨迹矩阵，
以及恒定比热和 Helmholtz EOS 下的独立弱反应轨迹验证。这些记录覆盖
同一套网络物理和 ODE 求解器在 CPU、CUDA 后端上的执行。

## 当前证据

以下四份报告对应同一份冻结源码，分别记录实际构建、生成网络包、求解器
依赖库及执行设置。科学精度与程序接入使用各自的验收标准；表中列出
适度舍入后的最大误差，验收预算保持不变。

| 报告 | 覆盖内容 | 最大实测误差与原预算 |
| --- | --- | --- |
| [恒定比热弱 Urca](results/weak-cv-native-20260907/release-898/evidence.json) | CPU/CUDA 上的 BE_NR、BD、ROS4 与独立轨迹比较 | 细容差组归一化状态误差 `3.3544e-8`，预算 `1e-7` |
| [Helmholtz 弱 Urca](results/weak-helm-native-20260907/release-899/evidence.json) | 同样六条轨迹路线，包含组分相关的热力学闭合 | 细容差组归一化状态误差 `6.5112e-13`，预算 `1e-7` |
| [真实 audit31 稀疏演化](results/sparse-native-20260907/release-900/evidence.json) | 三种 ODE、四个外部步、两格和三格存储；CPU KLU / CUDA cuDSS | 场误差 `1.8812e-14`，预算 `2e-10`；限步器误差 `1.8305e-14`，预算 `2e-8` |
| [生成网络 ARCH 应用](results/runtime-native-20260907/release-875/backend-validation-evidence.json) | 六个案例、48 次 CPU/CUDA 运行、24 次比较 | 归一化场差 `8.7290e-15`；原场容差 `rtol=2e-8`、`atol=1e-12` |

### 独立弱反应轨迹

受控 Na23–Ne23 Urca 对从相等质量分数组成出发，初始密度为
`rho=4e9 g/cm³`，温度为 `T=5e8 K`。恒定比热轨迹使用
`cv=1e8 erg/(g K)`，积分 10 秒；Helmholtz 轨迹积分 0.01 秒。
独立参考读取原始 Suzuki 表，自行插值，并分别使用 DOP853、Radau 积分。
温度演化遵循定密度第一定律，包含组分变化引起的内能变化和有符号弱反应源项。

科学误差衡量质量分数、`T/T_initial`，以及除以初始比内能尺度的弱反应源项积分。
恒定比热采用 `cv*T_initial` 作为能量尺度，Helmholtz 采用独立计算的初始比内能。
两种设置均保留原有 `1e-7` 科学预算。DOP853/Radau 的差异分别为
`3.3307e-16` 和 `1.4823e-21`；独立积分器一致性及能量恒等式的预算均为 `2e-11`，
最大独立能量残差分别为 `4.5750e-16` 和 `8.5639e-19`。

局部容差为 `1e-11` 时，三种生产 ODE 在两个后端上全部通过。将局部容差从
`1e-7` 收紧到 `1e-11`，BE_NR 的误差在恒定比热和 Helmholtz 下分别改善约
100.0 倍和 88.7 倍。恒定比热粗容差控制组的误差 `3.3556e-6` 仍作为
原 `1e-7` 科学预算下的失败控制保留，因此容差收敛结论可以直接复查。
细容差生产轨迹的能量闭合误差为 `2.5455e-14` 和 `2.4728e-10`，原上限为 `2e-8`。
这项闭合检查相对于加热量归一化，与独立状态比较采用的初始能量尺度不同。

轨迹比较在两端均使用 DenseLU。配套的类型化工厂检查还覆盖 CUDA DenseLU、
cuDSS 的数据所有权复用，以及两格、三格存储。两个报告中的工厂控制均使用
恒定比热，独立的 CPU/CUDA 场比较保留原 `2e-10 * max(1, |reference|)` 预算；
Helmholtz 科学轨迹使用真实 EOS。这些测试验证表格 Urca 的积分
路径；其他生成网络仍按各自的核素集合、反应数据和适用热力学范围进行评估。

### 稀疏求解与完整程序接入

audit31 包含 31 个核素和温度，共 32 个 ODE 方程，因此实际走生产稀疏路径。
定向记录对 BE_NR、BD、ROS4 分别使用 CPU KLU 和 CUDA cuDSS，将存储从
两格切换至三格，并记录四个外部步内的真实组分演化。它验证共用物理下的
求解器一致性与存储复用，独立反应数据参考承担另一类检查。逐步耗时用于诊断，
不作为受控加速比基准。

[应用清单](runtime_cases.json) 在真实 ARCH 程序中组合 audit31、weak_urca、
Helmholtz EOS 及三种 ODE。`linear_solver=Auto` 对 audit31 在 CPU 选择 KLU、
在 CUDA 选择 cuDSS；紧凑弱网络在两个后端均选择 DenseLU。两端都抵达指定物理
时刻：audit31 为 `1e-10` 秒，弱 Urca 为 `0.01` 秒。固定时间比较的最大归一化
场差为 `7.6473e-15`。全部样本中的最大质量分数和误差为 `2.2204e-16`，
原预算为 `1e-8`；密度和能量保持为正。

弱网络中间步的步数及控制器差异作为诊断记录，物理比较使用相同终止时间。
严格检查点恢复由单独的 restart 测试覆盖。DenseLU 的生产边界为 31 个
**完整 ODE 方程**，包含温度及可选的有符号源项积分；更大的系统使用
CPU KLU / GPU cuDSS。

### 设备安全覆盖

最终的 [memcheck](../backend/results/final-first-law-20260907/memcheck-929/evidence.json)
和 [racecheck](../backend/results/final-first-law-20260907/racecheck-903/evidence.json)
各通过全部 23 条路径，包含生成网络数学、受控弱反应轨迹、所有权复用及真实
audit31 稀疏演化。Memcheck 的 23 份完整报告均为零错误、零泄漏；racecheck 的
23 份完整报告均为零竞争风险、零错误、零警告。

| 稀疏插桩检查 | 观察区间 | 最大场／限步器误差 | 原预算 |
| --- | --- | --- | --- |
| Memcheck | 完整 `1e-10 s` | `1.8812e-14` / `1.8305e-14` | `2e-10` / `2e-8` |
| Racecheck | 显式 `1e-12 s` | `1.5222e-14` / `1.5148e-14` | `2e-10` / `2e-8` |

两组稀疏检查均保留三种 ODE、两格与三格存储、四段划分，以及完整的 24 条 CPU
和 24 条 GPU 步记录。密度、温度、比热、局部容差和初始组分不变。每种方法均有
真实演化和实际 kernel 执行，池容量稳定为二。仅 racecheck 的稀疏观察区间缩短，
其余 22 条命令及全部数值预算均不变。普通 audit31 稀疏轨迹、audit31 完整程序和完整区间
memcheck 仍保留 `1e-10 s` 终点。

[插桩说明](../backend/results/final-first-law-20260907/README.zh-CN.md)提供两种
受资源保护的复现命令、完整测试构建要求及独立记录复核。插桩耗时用于诊断，
不作为加速比基准；整体验收状态见[验证总览](../README.zh-CN.md)。

## 复现这些记录

先准备好[构建依赖](../../README.zh-CN.md#构建)，包括 EOS 数据文件和 cuDSS 0.8，
再从仓库根目录的 Linux/WSL 终端执行以下命令。归档中的生成网络包和独立参考
使用 Python 3.11 和 **pynucastro 2.12.0**。生成网络与运行参考工具应使用同一个 Python 环境，
确保读取同一套 Suzuki 表和核数据。

```bash
python3.11 -m venv build/network-python
source build/network-python/bin/activate
python -m pip install "pynucastro==2.12.0" numpy scipy mpmath

python tools/network/GenerateNetwork.py validation/network/inputs/audit31.py
python tools/network/GenerateNetwork.py validation/network/inputs/weak_urca.py

validation_build="$PWD/build/network-validation"
cmake -S . -B "$validation_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DPython3_EXECUTABLE="$(command -v python)" \
  -DARCH_ENABLE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=native \
  -DARCH_ENABLE_KLU=ON -DARCH_FETCH_SUITESPARSE=ON \
  -DARCH_ENABLE_CUDSS=ON -DCUDSS_ROOT=/path/to/cudss \
  -DARCH_CUSTOM_NETWORKS="audit31;weak_urca" \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$validation_build/bin"

python tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build "$validation_build" --parallel 1 --target \
    ARCH arch_cuda_single_level_validation \
    arch_cuda_generated_weak_trajectory_weak_urca \
    arch_cuda_generated_weak_factory_weak_urca \
    arch_cuda_generated_sparse_burn_audit31
```

将 `/path/to/cudss` 替换为实际安装目录；如果 CMake 已能找到该库，可省略
`CUDSS_ROOT`。已安装的 KLU 可通过 `CMAKE_PREFIX_PATH` 指定，否则 CMake
会获取已固定版本的 SuiteSparse 依赖。配置输出应同时包含 `cuDSS found` 和
`SuiteSparse KLU enabled`，因为这些记录需要两个求解器。上述五个构建目标
包括三个定向验证程序、ARCH 和检查点比较器。显式设置输出目录后，ARCH
才会位于下方命令使用的路径；默认输出位置是仓库的 `bin/`，而非构建目录的 `bin/`。

生成器将 [audit31](inputs/audit31.py) 和 [weak_urca](inputs/weak_urca.py) 的网络包
写入 `src/physics/network/custom/`，两个后端共用同一份注册包。
已经匹配的包不会改动。修改配方或生成设置后若要替换网络包，在对应生成命令后添加
`--replace`，生成器会保留备份。生成或替换网络包后需重新运行 CMake。

保持此 Python 环境激活，每次复现均使用新的空结果目录。下方命令采用报告
记录的四线程设置。构建并行数和[内存守护工具](../../tools/run_memory_guarded.py)
的限额可按可用资源调整，上方串行构建仅作为起始配置。

```bash
export OMP_NUM_THREADS=4

python3 validation/network/run_weak_validation.py \
  --build-dir "$validation_build" --output-dir validation/network/results/local-weak-cv \
  --network-id weak_urca --eos constant_cv \
  --rho 4e9 --temperature 5e8 --interval 10 --cv 1e8

python3 validation/network/run_weak_validation.py \
  --build-dir "$validation_build" --output-dir validation/network/results/local-weak-helm \
  --network-id weak_urca --eos helmholtz \
  --rho 4e9 --temperature 5e8 --interval 0.01 --cv 1e8

python3 validation/network/run_sparse_validation.py \
  --build-dir "$validation_build" --output-dir validation/network/results/local-audit31 \
  --network-id audit31 --rho 1e7 --temperature 3e9 --interval 1e-10 \
  --cv 1e8 --rtol 1e-7 --steps 4 --composition c12=0.5 o16=0.5

python3 tools/validate_backend_results.py \
  --source-root . --build-dir "$validation_build" \
  --arch "$validation_build/bin/ARCH" \
  --checkpoint-validator "$validation_build/arch_cuda_single_level_validation" \
  --manifest validation/network/runtime_cases.json \
  --output-root validation/network/results/local-generated-runtime
```

Helmholtz 命令中的 `--cv` 用于配套工厂控制，轨迹本身使用 Helmholtz 导数。
弱反应工具保留粗、细两档容差控制，并要求细容差结果通过。稀疏工具要求三种
ODE 和两种存储规模全部通过；单方法诊断或跳过 GPU 均不算完整覆盖。

## 适用范围与后续工作

完整 Release／Debug 回归、[五阶段核心构建检查](../backend/results/cold-core-first-law-20260907/release-909/README.zh-CN.md)
及[声明规模的容量检查](../backend/results/device-memory-first-law-20260907/README.zh-CN.md)
已通过，上述两组定向设备安全检查也已完成。最终交付审阅和汇总验收记录由
[验证总览](../README.zh-CN.md)统一跟踪。四份科学／程序接入记录与插桩记录
分别保留各自的验收范围。

150/200 核素的完整轨迹和规模测试仍按已批准的安排，放到更大验证系统上继续，
不属于本轮本地发布门槛。超大网络的科学可靠性取决于核素集合、反应数据和模型
适用范围，求解器及后端的一致性单独记录。当前不支持自定义网络 NSE；
[NSE 独立参考](nse_reference.py) 覆盖已支持的内置网络。

内部状态见[发布标准](../../docs/development/CudaReleaseStandard.md)，
用户能力说明见[后端指南](../../docs/CudaBackendStatus.zh-CN.md)。
