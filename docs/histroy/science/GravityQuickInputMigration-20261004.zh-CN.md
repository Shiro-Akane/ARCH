# CPU gravity quick 输入迁移：原有 Cartesian 样本

## 结果与覆盖范围

三个既有样本的配置 v3 inspect 均 exit 0，随后原有实际 CPU 轨迹及原预算 PASS。
这只是完整 self_gravity_physics 的局部证据，不是 full gate / CPU 阶段通过。
全 CTest 原 69/71 失败记录保留；径向 historical gravity_G=1e-20 换算仍待 Core。

## 输入迁移依据

旧 commit 8fc0dd25eefd2243e8c36f85440bac46994e2e73 的 GlobalDefs.h 默认，
由 StandardParameters.h 注册、RuntimeParams.h Get* 实际读取：
diff_cfl=.8，use_viscous_diff=false，use_species_diff=false；
nuclearTempMin=1e9，nuclearDensMin=1e-10，smallt=1e5，smallx=1e-20，
enucDtFactor=1e30，eos_coulomb_mult=1。

thermal-linear-4 显式补 diff_cfl 及两个 inactive channel；
burning_config 显式补原 burn/EOS 默认；
coupled diffusion 显式补 diff_cfl 与 use_viscous_diff=false。
已有 use_species_diff=true 保持，不为 Helmholtz 插入 forbidden transport 系数。
调用方 changes 的 override precedence 保持；没有改任何独立参考或预算。

## 处理后指标

| 样本 | 指标 | 原预算 |
| --- | --- | --- |
| thermal-linear-4 | independent linear relative error 0.0002614262793 | <0.02 |
| gravity-burn | energy-minus-nuclear 8.8905823e-13；mass drift 1.9792111e-16 | <=1e-6；<=1e-12 |
| gravity-burn-diffusion | energy-minus-nuclear 8.9234980e-13；mass drift 3.3762056e-16 | <=1e-6；<=1e-12 |
| paired transport effect | 8.2158963e-7 | >1e-8 |

两种 burn 的核热和组分变化均 >1e-8；所有 gravity solve residual 通过原 target、
density lease 不复用、floor repair 为0，按既有 BoxCampaign.run 检查。
完整计时、RSS、输入 hash、binary identity 在同名 Summary.json。
当前主 executable 来自 clean b82205e2，SHA 7d0360de4ac9a3429d2a8ffec3908ca4424f2c7716096f2c44ad189662a9d8c4；
此次仅 Python validation 输入改动，无需重新 build。

## 复现与保留

原 runner validation/gravity/gravity_box.py 的 diffusion_reference、
coupled('gravity-burn',False,**compact_config())、
coupled('gravity-burn-diffusion',True,**compact_config()) 仍是验收实现；
两份末态 TEMP/ENER/c12 按原 run_checks 的 paired transport 公式比较。
完整入口仍为 validation/gravity/run_self_gravity.py --quick，不跳过径向项替它置绿。

本次先 capture 原生成器输入并用 --inspect-config GravityBox --config-stdin 检查，
无 Setup/Init 副作用；均通过后才执行以上三条已指定轨迹。
原始 input、inspect 响应、run logs、H5/checkpoint、局部 orchestration script 保留本机
studio/.local/integration/gravity-quick-input-migration-20261004，不提交。
未重复已经通过的 cloud/Jeans 或整套 CTest；未运行 CUDA、新 O9 场景或 push/tag。
