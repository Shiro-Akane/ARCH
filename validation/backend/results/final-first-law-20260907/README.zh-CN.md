# 后端专项设备安全检查

英文原文：[README.md](README.md)。英文版为规范文本。

[Memcheck 记录](memcheck-929/evidence.json)与
[racecheck 记录](racecheck-903/evidence.json)均通过全部 23 条专项路径。
Memcheck 全部为零错误、零泄漏字节及零泄漏分配；racecheck 全部为零竞争风险、
零错误、零警告，每条路径均有独立、完整的报告。
[Memcheck](memcheck-929-review.json)和 [racecheck](racecheck-903-review.json)
的独立只读复核使用既有验证工具，核对了准确的可执行文件清单、命令参数、文件身份
及稀疏轨迹覆盖。

| 检查组 | 状态 | 稀疏观察区间 | 覆盖情况 |
| --- | --- | --- | --- |
| [Memcheck](memcheck-929/evidence.json) | 已完成 | 原默认值 `1e-10 s` | 23 条路径，23 份完整且无报错的报告 |
| [Racecheck](racecheck-903/evidence.json) | 已完成 | 显式 `1e-12 s` 配置 | 23 条路径，23 份完整且无报错的报告 |

两种配置将插桩观察时长与科学验收终点分开。racecheck 的观察区间不替换完整区间的
[audit31 轨迹](../../../network/results/sparse-native-20260907/release-900/evidence.json)
或 [ARCH 应用](../../../network/results/runtime-native-20260907/release-875/backend-validation-evidence.json)
检查。整体发布验收还包含[验证指南](../../../README.zh-CN.md)中的完整程序安全记录与最终交付审阅。

## 已完成的插桩覆盖

清单由 19 项已配置测试和四条显式生成网络路径组成，覆盖 AMR 事务、迁移、交换、
组分和几何，稀疏求解器，EOS 失败处理及主机／设备一致性，内置 NSE 与燃烧热力学，
生成网络数学，aprox19 的三种 ODE，受控弱反应轨迹与所有权复用，以及真实 audit31
稀疏演化。实际命令与 [98 项 Release 清单](release-regression-895/evidence.json)一致。

两组检查的完整源码、构建和执行设置记录均与该 Release 记录及普通 audit31
轨迹记录一致。23 份程序身份及每组 23 份互不重复的安全报告均已核对；相对原命令
参数，仅 racecheck 的稀疏观察区间发生变化。冻结脚本的 SHA-256 为
`a4c505dcf3e1507d5bb7888a90f1e7663d78040f11719993f228dc9fa2e32be8`。

### Memcheck 下的完整区间稀疏轨迹

audit31 保留 `rho=1e7`、`T=3e9`、`cv=1e8`、局部容差 `1e-7`，初始 C12/O16
质量分数各占一半。总区间仍为 `1e-10 s`，划分为四段；BE_NR、BD、ROS4 各自
覆盖两格与三格存储，CPU 使用 KLU，CUDA 使用 cuDSS。共同的轨迹解析器核验了
24 条 CPU 步记录、24 条 GPU 步记录、六个终态及三份方法摘要，没有省略 ODE 或存储组合。

| ODE | 最大场误差 | 最大限步器误差 | 记录的 CUDA kernel 启动次数 | 每计算通道工作区字节数 |
| --- | ---: | ---: | ---: | ---: |
| BE_NR | `5.3164e-15` | `5.4667e-15` | 137,946 | 7,204 |
| BD | `1.8812e-14` | `1.8305e-14` | 6,164 | 23,740 |
| ROS4 | `5.1266e-15` | `5.0365e-15` | 8,932 | 11,852 |

原有场误差和限步器误差预算仍为 `2e-10` 与 `2e-8`。三种方法均有可测演化，
其中最小的方法演化指标为 `2.56994e-5`；两种存储规模下池容量均保持为二。
这些插桩结果检查求解器和后端执行，与普通运行共用同一套数学。
kernel 次数与插桩耗时用于诊断，不作为受控性能比较。

### Racecheck 下的稀疏观察

Racecheck 显式使用 `1e-12 s` 观察区间。密度、温度、比热、局部容差、初始组分、
四段划分、三种 ODE 和两种存储规模均保持不变。其记录同样包含全部 24 条 CPU 步、
24 条 GPU 步、六个终态及三份方法摘要。

| ODE | 最大场误差 | 最大限步器误差 | 记录的 CUDA kernel 启动次数 | 每计算通道工作区字节数 |
| --- | ---: | ---: | ---: | ---: |
| BE_NR | `8.2953e-15` | `7.0884e-15` | 3,820 | 7,204 |
| BD | `1.1074e-14` | `1.1026e-14` | 2,252 | 23,740 |
| ROS4 | `1.5222e-14` | `1.5148e-14` | 492 | 11,852 |

原有 `2e-10` 场误差和 `2e-8` 限步器误差预算均通过。三种方法均有实际 kernel
执行和可测演化，最小的方法演化指标为 `2.54829e-7`，池容量仍为二。
该记录检查真实稀疏演化期间的设备竞争；科学验证和完整程序验收仍保留原有
`1e-10 s` 终点。

## 复现

从仓库根目录执行，准备好启用测试的 CUDA 构建、已注册的 audit31 与 weak_urca
网络包、CPU KLU、CUDA cuDSS 和所需 EOS 数据。网络生成与构建配置见
[网络准备说明](../../../network/README.zh-CN.md#复现这些记录)。这些记录及验收索引
使用 Python 3.11，以保持诊断耗时求和的舍入结果一致。

运行 23 路脚本前需构建完整的已配置测试清单；网络指南中的五个显式目标仅覆盖
其定向科学验证和完整程序命令。对下方使用的构建目录，受保护的默认构建会包含
其余测试：

```bash
python3 -B tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build build-cuda --parallel 1
```

为每次运行选择新输出目录，在现有资源保护下串行执行 GPU 检查。
`--sanitizer` 与 `--nvidia-smi` 分别指向实际安装的程序。
显式超时值是每条路径的允许时限，不是预计运行时间：

```bash
OMP_NUM_THREADS=4 python3 -B tools/run_memory_guarded.py \
  --min-available-mib 1536 --max-swap-growth-mib 256 --pressure-guard \
  --gpu-memory-device 0 --nvidia-smi /path/to/nvidia-smi \
  --log build/focused-memcheck-new.log -- \
  python3.11 -B validation/backend/results/final-first-law-20260907/run_sanitizers.py \
  --build-dir build-cuda \
  --output-dir validation/backend/results/focused-memcheck-new \
  --sanitizer /path/to/compute-sanitizer --tool memcheck --timeout-seconds 2400

OMP_NUM_THREADS=4 python3 -B tools/run_memory_guarded.py \
  --min-available-mib 1536 --max-swap-growth-mib 256 --pressure-guard \
  --gpu-memory-device 0 --nvidia-smi /path/to/nvidia-smi \
  --log build/focused-racecheck-new.log -- \
  python3.11 -B validation/backend/results/final-first-law-20260907/run_sanitizers.py \
  --build-dir build-cuda \
  --output-dir validation/backend/results/focused-racecheck-new \
  --sanitizer /path/to/compute-sanitizer --tool racecheck --timeout-seconds 86400 \
  --sparse-race-interval 1e-12
```

第二条命令复现已完成的 racecheck 观察配置。
不指定 `--sparse-race-interval` 时，两种工具都保留完整 `1e-10` 区间。
该选项仅允许用于 racecheck，且必须为正有限值。其余 22 条命令、稀疏输入、四段
划分、三种 ODE、两种存储规模和数值预算均保持不变。

[脚本自测](test_run_sanitizers.py)仅使用 CPU，运行命令为
`python3.11 -B validation/backend/results/final-first-law-20260907/test_run_sanitizers.py`。

## 保留的早期尝试

[尝试 902](attempt-902/README.md)完成 22 条路径后，稀疏路径超过原有时限；
[尝试 916](attempt-916/README.md)在完成清单前被资源监控停止。原失败记录保持完整，
929 是已完成的 memcheck 后继记录。[配置调整前的脚本档案](profile-history/full-interval/README.md)
保留原有默认值及显式 racecheck 观察选项的说明。
