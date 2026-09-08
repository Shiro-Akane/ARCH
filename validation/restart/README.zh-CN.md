# HDF5 检查点与续算连续性

英文权威文本见 [README.md](README.md)。

本页结果对应[Validation 总索引](../README.zh-CN.md)注明的科学验收版本；后续目录维护
及新构建检查单列于[维护记录](../backend/results/maintenance-freeze-20260908/)。

CPU 与 CUDA 共用一种检查点格式和读取器。只要目标构建支持相应物理模块和数据，
就可以从检查点切换后端继续运行。验证将续算与不间断运行进行比较，包含动态加密和
燃烧。整体验收状态统一见[验证索引](../README.zh-CN.md)。

## 检查点保存什么

v4 保存 AMR 叶块拓扑、守恒场、原始质量分数和用于加密判据的 `ENUC`，同时保存时间步
控制状态、输出编号和循环阶段，避免续算时重复执行已经完成的重网格或输出操作。
质量分数与组分密度同时保存，恢复时不再经过先乘密度再除密度的有损舍入过程。

文件还记录 EOS、表内容或理想气体参数、网络与 NSE 选择，以及有序组分信息。
读取器先检查这些物理配置是否一致，再恢复状态。CUDA 随后通过常规后端接口上传
恢复的场，不采用另一套 IO 格式。

v1/v2 文件仍可读取，但不含 ENUC 和 v3 的物理配置身份记录，v1 还缺少部分控制器
状态。缺失的 ENUC 从零开始，因此无法还原文件中没有保存的 ENUC 加密历史。
v3 保留这些状态和物理配置身份，但质量分数需要从组分密度重建。
请使用 v4 检查点，以同时保留实际演化的原始组分。

## 发布候选版本已完成的检查

[光滑平流记录](../amr/results/restart-smooth-native-20260907/release-876/restart-validation-evidence.json)
和[燃烧／ENUC 记录](../amr/results/restart-burn-native-20260907/release-877/restart-validation-evidence.json)
使用同一份候选源码、程序和比较工具，各通过十二次执行和九次比较。每组包括两次
不间断运行、两次检查点来源运行和八次续算。续算覆盖 CPU 到 CPU、CPU 到 CUDA、
CUDA 到 CPU、CUDA 到 CUDA 四种方向，分别从第 2 步重网格后的检查点和第 3 步
终态检查点启动。全部续算推进至第 4 步，保留 0–1 混合 AMR 层级；平流有七个
叶块，燃烧有八个。燃烧采用 aprox13 网络、Helmholtz EOS，并由 BE_NR 配合
DenseLU 求解。

全部比较均包含守恒场、原始 `X`、`rhoX`、`ENUC`、时间步控制状态和输出历史。
下表包含一次不间断 CPU/CUDA 对照及八次续算比较：

| 案例 | 执行／比较次数 | 最大场误差／各自场峰值 | 最大 ENUC 误差／ENUC 峰值 | 燃烧步长上限最大相对误差 |
| --- | ---: | ---: | ---: | ---: |
| 光滑平流 | 12 / 9 | 0 | 0 | 0 |
| aprox13 燃烧 | 12 / 9 | 6.523e-13 | 3.985e-13 | 3.984e-13 |

同后端续算与不间断参考的场差为零。跨后端燃烧续算的最大场峰值归一化误差为
`1.661e-13`。全部路径均通过拓扑、循环阶段、时间与控制器检查。中间检查点续算
保持原输出编号；终态检查点续算恰好多一个 checkpoint 和 plot，这一差值由实际
停止运行时的输出历史确定。

燃烧／AMR 的 [memcheck 检查](../amr/results/restart-burn-native-20260907/memcheck-905/restart-validation-evidence.json)
和 [racecheck 检查](../amr/results/restart-burn-native-20260907/racecheck-907/restart-validation-evidence.json)
也在同一候选版本上各自完成全部十二次执行和九次严格比较。每组对六次实际 CUDA
执行插桩：不间断运行、检查点来源运行，以及四次以 CUDA 为目标的恢复。
六份 memcheck 报告均为零错误、零泄漏字节和零泄漏分配；六份 racecheck 报告
均为零隐患、零错误和零警告，CPU 路径保留普通参考运行。原生组分、控制器状态、
循环阶段和由来源历史确定的输出编号差均通过原有检查，数值误差峰值与上表普通
燃烧记录一致，同后端续算的场差仍为零。这两组重启插桩检查已完成。

### 严格连续性预算

这些报告使用严格可复现比较，不使用固定物理时刻的科学比较模式。步数与拓扑必须
完全相同，时间和 `dt_old` 使用独立的 binary64 舍入预算：本次第 4 步允许八个 ULP。
各场的最大绝对差不得超过 `5e-12 + 5e-9 × S`，其中 `S` 为两个检查点中该场的
最大绝对值。ENUC 保留原定 `5e-12 + 1e-3 × S` 预算；燃烧步长上限保留
`1e-3` 相对容差和 `5e-12` 绝对容差。输出编号差由已保存的来源历史核对，
不通过任意平移使比较通过。HDF5 内容通过共同读取器进行比较，容器分配和元数据
布局不属于数值状态。

## 持续原生状态恢复

[当前候选版本的持续记录](../amr/results/sustained-first-law-20260907/release-901/evidence.json)
通过 CPU、CUDA 和交替后端三条燃烧链各十二个恢复循环。36 份来源状态在继续演化前，
分别经过共同的 Host 读取器和 CUDA 上传／下载路径恢复。全部 72 次原生状态比较中，
场、原始组分、时间、控制器及输出元数据均精确保持，场差为零。

24 次同后端向前续算比较的场差也为零。交替链保留十二次局部向前步诊断，但这些
诊断不替代物理验收。六个续算终点和一次不间断 CPU/CUDA 对照均在指定的
`t=1e-10` 通过，最大场峰值归一化差为 `7.673e-13`，满足前述原定场及 ENUC
预算。精确恢复、严格同后端续算与固定物理时刻比较仍分别检查。

光滑平流链另通过十二次交替后端恢复，推进至第 26 步，十四次历史与终点比较的
场差均为零。更长的流体与扩散重网格运行见 [AMR 记录](../amr/README.zh-CN.md#持续重网格与续算)。

## 复现动态 AMR 检查

使用开启测试的 CUDA 构建，准备好 `ARCH` 和
`arch_cuda_single_level_validation`，在仓库根目录执行，并选择新的输出目录：

~~~bash
python3 tools/validate_cuda_amr_restart.py \
  --arch build-cuda/bin/ARCH \
  --checkpoint-validator build-cuda/arch_cuda_single_level_validation \
  --source-root . --build-dir build-cuda \
  --problem SmoothAdvection --input validation/amr/inputs/smooth_amr80_l1.par \
  --output-root validation/amr/results/restart-smooth-new
~~~

燃烧检查改用 `--problem BurnGradient`、
`--input validation/amr/inputs/burn_enuc_amr.par`，并换一个输出目录。
统一工具会生成不间断与续算分支，并核对源检查点实际保存的步数和循环阶段。

对燃烧命令添加
`--cuda-sanitizer /path/to/compute-sanitizer --sanitizer-tool memcheck`
即可进行对应插桩检查；另选新输出目录并改为 `--sanitizer-tool racecheck` 再执行。
同一工具保留四个后端方向、两种来源阶段和全部原有数值预算。

上述续算测试不包含在外部库调用中途强制中断。

## 历史记录

早期[平流](../amr/results/restart-smooth-native-20260907/release-758/restart-validation-evidence.json)
和[燃烧／ENUC](../amr/results/restart-burn-native-20260907/release-757/restart-validation-evidence.json)
报告保留原有源码与程序身份。[早期持续记录](../amr/results/sustained-first-law-20260907/release-763/evidence.json)
中的恢复与固定时刻测量保留原样，与上方候选版本记录分别归档。

[metrics.csv](metrics.csv) 保留早期 CPU 均匀网格及格式兼容审计，不代表当前候选
的跨后端测量。[审计说明](results/pre-release-notes-20260907/README.zh-CN.md)保留了
原始输入、兼容细节与复现命令；当前应用指标见上方两份候选报告。
