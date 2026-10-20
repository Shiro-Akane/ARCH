# BoxCampaign 完整 CPU 增量：孤立云与耦合时间阶

## 原范围与实际结果

执行原 run_checks(quick=False) 相对已经通过quick的全部新增路径：
cloud2/4、cloud-domain/expanded、cloud-mixed及coupled-time4/8/16/64/128，
共10条新实际CPU记录PASS。没有重新运行未改变的cloud1/thermal/compact burn。
原cloud1处理后summary SHA明确复用；只作收敛序列首点，不称为新运行。

原CPU source build b82205e2、ELF SHA
7d0360de4ac9a3429d2a8ffec3908ca4424f2c7716096f2c44ad189662a9d8c4。
最新源码380ce5a9的错误诊断尚未进主ELF；本轮无重build或科学源码变化。
实际24秒，memory guard无swap growth/stop，峰值owned RSS约3.57GiB；
per-run /usr/bin/time峰值约3.81GiB。两个不同采样计数如实分别保留，
不能作为无遗漏的硬RSS上限或正式性能验收。

## 冻结预算

| 验证 | 实际指标 | 原要求 |
| --- | --- | --- |
| Gaussian势收敛 | 2.041186 /2.012819 | >=1.8 |
| Gaussian力收敛 | 2.010890 /1.999534 | >=1.8 |
| 同dx扩大域common-core势误差 | .00915359→.000654946 | 减小 |
| mixed cloud势／力相对RMS | .00142745／.00444441 | <.08／<.12 |
| coupled时间阶 | 2.017102／2.070395 | >=1.8 |
| coupled参考细化变化 | 4.33257e-10 | <.1×最小主误差 |

mixed cloud实际有不止一个leaf level。cloud均原t=0初始化；
coupled-time均原t=1e-6，共4/8/16/64/128宏步设置，nuclearTempMin原1e8。
coupled所有原mass/energy-minus-nuclear预算、核热/组分变化、
Poisson residual/density lease与零floor检查通过。
每项基于完整原生数组，显示或稀疏抽样没有替代范数。
独立参考、输入物理、阈值都未改；没有引入新方法/场景。

## 复现与交付

复用validation/gravity/gravity_box.py的cloud、temporal_coupling及
run_checks非quick内原Gaussian/domain-expansion公式；不新增CI任务或绿色替代套件。
local orchestration在studio/.local/integration/box-full-cpu-20261004，
Summary含source/binary/generator SHA、每输入hash、处理后结果与原始H5/checkpoint
相对文件名/size/SHA的持久本地索引；原始场、ELF、完整日志只留本机。

fetch后的上游refs仍8fc0dd25 /23ff77c4f，无新的Core策略确认，未merge。
完整CPU gate仍有3D Jeans coarse ratio冲突、radial historical G换算及其他科学待审；
本Box增量PASS不能升级为full self_gravity、CPU、CUDA、RZ/JENS或O9验收。
原失败JUnit/log原样保留。无Windows、push/tag。
