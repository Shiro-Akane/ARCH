# 当前全配置 CPU 回归与迁移 finding

基线799ecb806e4c97ba37276fbc90f6d9f09d6bde92，开始clean。
现有build-cpu Release/CUDA OFF/KLU ON；不新建测试tree，不调整科学flags/阈值。
全Host目标以8并发和2048MiB余量、256MiB swap增长/PSI保护构建。

## 首次构建失败与修复

原test_resolved_execution_plan仍要求公开SimConfig+单独SpeciesManager可launch，
与已落地RuntimeConfiguration私有构造边界矛盾，编译static_assert失败。
迁移正向签名并保持无context/缺provenance/pointer拒绝，
新增raw-config、legacy species参数入口、公开构造/default构造负向断言。
未修改生产入口以迎合测试；定向编译成功，后续完整CTest该项PASS。

续建38步骤完成，guard未触发、swap0，最小可用约19GiB。
原CMake Python /usr/bin/python3缺NumPy；关联到已有venv，NumPy2.5.3/h5py3.16.0。
cache前后只有Python发现相关项变化；科学编译选项未变。
不把缺依赖解释为数值失败，不跳过test。

## 原始完整结果

71项全部执行，69 PASS / 2 FAIL / 0 skip，约118.33s。
失败：self_gravity_physics、ui_expansion_contract。
现有check_ci_results严格核对原inventory/JUnit，明确拒绝nonzero failures。
未合成绿JUnit，未将局部复验改写为完整71/71。

ui_expansion的两项陈旧断言仍认为Sedov无field/AMR支持。
按已实现全模型契约改为真实支持及Sedov root AMR正向检查，
保留unknown-case及restart非法拒绝。第一次新unknown-case预期code4不符真实v3
configuration阶段code3/detailCodeUNKNOWN_CASE；核对实际response后明确Setup未执行。
最终定向9case套件1/1PASS、4.75s；不重复其余已通过70/69项。

## Gravity输入迁移

self_gravity_physics先通过已有Jeans quick，再在cloud-1配置阶段失败：
x2_min/x3_min、center_y/center_z缺失。
更重要的是旧cloud generator未显式center_x，
基础1D .par迁移后center_x=5e7会覆盖原unit-domain的实际默认.5。

已核对origin/compute/optim原GravityBox Setup：
Cartesian中心=lower+.5*length；cloud独立oracle也使用extent/2。
cloud_config明确三轴lower0、center=extent/2，并保留显式changes优先级。
不是猜测新物理默认，也不改原Gaussian参考或预算。

只重新执行原quick cloud-1 t=0：
potential relative RMS .006732694362631971 < .08；
force relative RMS .037610239604195655 < .12，
原解析门槛保持。此局部通过不能代表整个self_gravity_physics。

剩余campaign还有有效输入迁移；radial_1d.py历史gravity_G=1e-20
科学等效换算仍由Core确认，未删除键/改floor/关闭gravity或改阈值绕过。
因此完整CPU gate保持INCOMPLETE，不提前统一CUDA或O9认证。

## 证据边界

主ELF仍7d0360de4ac9a3429d2a8ffec3908ca4424f2c7716096f2c44ad189662a9d8c4；
本次生产科学Core未修改，只迁移契约测试/验证生成器。
原inventory/JUnit、完整CTest日志、首轮编译错误tail、cache前后与原H5
留studio/.local/integration/current-full-cpu-regression-20261004；
CTest的原self-gravity文件在ignored build-cpu/self-gravity-jeans保持。
只提交处理后摘要及源码。未push/tag/main merge。
