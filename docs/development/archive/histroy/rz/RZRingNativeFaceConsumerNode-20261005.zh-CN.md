# RZ 有限环体原生边界面消费节点（2026-10-05）

## 结论与交付范围

SCOPED CPU PASS；production RZ capability gate 保留。本节点将已存在的有限环体源区间接入真实 CompositePoisson exterior face center，并将已认证的边界误差交给原生 RHS 传播接口。它不是完整 RZ self-gravity 签收，不启用 production values()，不声称求解完成。

依据科学清单第 6–8 节：JENS frozen uniform-lifecycle-1；环体为原生叶单元完整旋转体，使用共享 CGS G；RZ 角动量消费者与环体边界证明按依赖分别推进。冻结物理定义和阈值未改变。

## 源码、构建与数据身份

本次 build 前 Git baseline：5711afe4c66299b9c4cae0469d3250cca816ff05。构建时存在本节点四个已授权源码/测试修改，完整文件 SHA-256 见 summary.json，不能将 ELF 误称为未修改 baseline 的产物。

新 CPU ARCH ELF SHA-256：d83186386a3dbcb403f473426da7dd42d1fc42840b09f7ddc43a7977fb36d95e。
旧 ELF SHA-256：1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。

复用 /home/arch/projects/ARCH-compute-optim/build-cpu，CPU Release、CUDA OFF。仅标准增量构建 ARCH、arch_composite_poisson、arch_self_gravity、arch_gravity_stage_contract；未 configure、未复制 build tree、未创建新工作区。

原始日志、H5、plot/checkpoint 仅存本机：
studio/.local/integration/rz-ring-native-face-20261005。
提交 summary.json 的标量证据，不提交场数组、ELF、原始日志或 checkpoint。

## 消费与身份约束

- GravityBoundary 绑定完整 mesh、boundary kind、有序 native cell 列表及 topology；同长度不同区域/次序不能复用。
- checked source cache 复用 GravitySolveIdentity 的输入、时间、版本、allocation generation、operator/boundary/accuracy revision 校验；source valid 不等于 field solved。
- GravityFieldValidity 仍独立要求 Complete token 与非零 publication storage generation。没有伪造完成状态。
- 每次成功更新增加 source generation；失败更新同样退役旧 source association。
- 密度及 moments 先完整检查后提交，保留 moments allocation，避免既有引用/upload view 悬空。
- 每个真实 exterior face 遍历所有有限叶源，用 operator.center ± width/2 构造与 unit moments 相同的源边。
- FP64 interval reduction、整面预算都通过后才统一认证。work/precision failure 仅保留诊断；Unknown 误差无法通过 RHS certificate。
- 几何 opening 不替代证书；尚未认证的 parent moment arithmetic 不用于远场接受。

## 可复现 scoped checks

实际命令（在现有 build-cpu）：

    ./arch_composite_poisson ring-native-face
    ./arch_composite_poisson ring
    ./arch_composite_poisson ring-axis-enclosure
    ./arch_composite_poisson ring-enclosure
    ./arch_composite_poisson boundary-ledger

实际 CLI 及最终 exit code 以本机 checks-corrected.json 和各日志为准。CTest：gravity_stage_contract、composite_poisson_analytic、self_gravity_lifecycle、composite_poisson_contract，4/4 PASS。tools/audit_architecture.py PASS。git diff --check PASS。

uniform/mixed native cells × radial origin 0/0.5 共四个 fixture：16/28 leaves、12/16 exterior faces、192/256/336/448 source evaluations。每个 fixture 的独立 full-azimuth Newton product quadrature 在一个实际 exterior face 取 8/12 两阶，两值都落在区间内；阶次差只作诊断，不当作可证明误差或科学参考收敛结论。

工程 fixture 使用 face absolute target=1e-5 cm²/s²、maximum boxes=8，专门验证真实接口、映射和失败传播，不是 production tolerance。边界-only RHS norm error 为约 6.7274e-6～7.5338e-6 s^-2；势和 RHS 单位不能混用。

反例覆盖：新时间/state version/allocation、不同 mesh/ordered cells、source generation 更新、负密度失败、topology 变化、global work cap、零目标非零源失败与 exact-zero 源成功。production values() gate 的拒绝仍通过。

历史验证尝试保留：误用 ring-moments CLI 导致 stoi 错误，改用既有 ring CLI 后通过；最初 axis-only reference 条件没有覆盖真实 exterior face，已改为每个 fixture 的实际 exterior face 并重新验证。旧日志保留，不充作最终 PASS。

## JENS 冻结短包

由于本次 ARCH ELF 改变，复用现有 BoxCampaign.jeans_uniform_lifecycle() 重做匹配身份的冻结短包；没有改阈值或扩展样本。1D/2D/3D × active/disabled/output-only：9 个连续演化，加 9 个真实 checkpoint restart。

全部严格到 t=0.02，真实 checkpoint split t=0.01。restart native state exact、均匀字段 exact；mass relative drift=0、energy drift=0；active accepted transaction steps=29/55/83；JENS 最大相对误差 4.50750606346323e-17。disabled/output-only 的 native state 和 solve counts 全部 exact。该 PASS 只覆盖限定均匀 Cartesian single-caloric-species CPU 短演化，不是非均匀 JeansWave、RZ 或 CUDA 签收。

memory guard 未停止，swap 增长 0；JENS 整包约 50.11 s 是本机 guard elapsed observation，不是性能基准或单模型长跑预算。

## 后续门槛

1. 非轴近场严格预算与效率；远场 parent moment/translation arithmetic 和尾界认证。
2. 完整 RHS assembly/coefficient construction/evaluation ledger 与原始 residual gate。
3. 实际 AMR density/topology source-publication 绑定，失败缓存/重建全链验证。
4. RZ A→B→C→D 全消费者及独立收敛验收；RZ-VISC-01、RZ-AXIS-01 待 Core 明确的参考/预算保持独立。
5. 对应 CPU 科学 gate 通过后统一 CUDA；输入、终点和预算冻结后才启动对应长跑和计时。

没有开启 production RZ、CUDA 长跑、Windows 适配，也没有更改 tag。
