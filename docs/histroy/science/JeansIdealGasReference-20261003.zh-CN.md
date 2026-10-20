# Jeans 与真实 IdealGas 闭合的独立 CPU 参考

2026-10-03，基线 30820aebd7fc48446dcda65100c111e74e531fc4。
git fetch origin 成功；origin/compute/optim 仍为 8fc0dd25eefd2243e8c36f85440bac46994e2e73，未 merge，也未取得新增科学批准引用。

## 验证链
test_jeans_diagnostics.cpp 使用真实 IdealGas::get_pressure / get_sound_speed，再将返回的声速平方传给 evaluate_cell。
独立参考由 caloric IdealGas 定义 c_s²=gamma*(gamma-1)*e 得出，并用 long double 的 pi、已发布 CGS G、总密度和明确 h=.5 计算 N_J；参考不调用待测 EOS、GridMetrics 或 Jeans 数学。
24 个组合：两个闭合（无组分gamma=1.5；双组分）、三种密度(.25,1,4)、两种内能(4,16)、静止/三分量运动。
运动状态直接构造守恒量（含动能），不调用 EOS primitive-to-energy 生成期望状态。
双组分 X=[.25,.75]、Cv=[2,4]、gamma=[1.5,2]；独立 Cv 加权 gamma=27/14，与构造 fallback=1.4 不同，验证未把固定gamma当作组成闭合。

## 实际结果
cmake --build build-cpu --target arch_jeans_diagnostics --parallel 4：PASS。
ctest --test-dir build-cpu -R '^jeans_diagnostics$' -V：1/1 PASS。
24 个新增参考最大相对误差 1.9657272738823614e-16。
沿用现有叶函数工程界限16*double epsilon；不是新的科学预算、不放宽原验收阈值。
首次测试输出换行出现多字符常量警告，已修正；最终编译无该警告且原精度结果重测通过。
git diff --check PASS。未改生产 Core/EOS、Host/Studio 或公开能力，不重复不受影响回归。

## 覆盖边界
这补足隔离数学与已知 IdealGas 的静态解析检查，不验证 Helm/Tabular 声速、接受宏步、AMR标志/候选父态、regrid/restart或演化科学预算。
无生产消费者，JENS 仍 unavailable。一般EOS/父态/条件规则/冻结参考与预算继续 pending。
未运行 simulation、Preview、CUDA 或重新编译生产 ARCH。完整本地日志仅留 studio/.local/integration/jeans-idealgas-reference-20261003/。
