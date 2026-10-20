# Jeans 共享数学叶函数 CPU 进度

2026-10-03，基线 a06b8d512eeaf0fc76495b1f74f671bbb012d93e。

## 实现范围
src/physics/diagnostics/JeansDiagnostics.h 定义独立 ARCH_INLINE 数学叶函数：
N_J=sqrt(pi*c_s²/(G*rho))/max(h_active)，G 从共享 PhysicalConstants.h 引用。
调用方传入总密度、声速平方、最大活动物理间距；叶函数不选择 EOS、不遍历网格、不选择阈值或 refinement flag。
使用 frexp/ldexp 分离二进制指数，避免 G*rho 中间下溢或 c_s²/rho 中间溢出导致可表示最终结果被误判。
数值输入域限定有限严格正值；非法输入与无法表示为正有限 double 的结果返回不同状态，无 floor/epsilon 或可写 G。
这不是对零声速 EOS 状态的科学判定；未来消费方如何处理须遵循批准的状态规则。

该函数尚无生产消费者，不开放 JENS capability，也不改现有拒绝入口。
一般 EOS 闭合、候选父态、条件必填与阈值边界、独立科学参考/预算仍待 Core 确认。
前序桌面矩阵尚未完整关闭，因此这里只提交隔离的数值基础，不宣布进入生产 AMR 或通过 O7.1。

## 独立参考与验证
validation/gravity/jeans_numeric_reference.py 用 Decimal 和独立高精度 pi / 已发布 CGS G 计算参考。
输入 double 通过 Decimal.from_float 精确转换，80/120 位参考舍入到同一 double；
不调用待测 C++ 实现、EOS 或 GridMetrics。
8 个参考包括密度×4、声速平方×4、间距×4、极低正密度以及指数抵消。
12 个非法输入位置组合和最终 overflow/underflow 状态检查通过。
CPU 比较界限 16*double epsilon 是本次数值叶函数工程检查，未修改既有预算，不是 EOS/AMR/轨迹科学验收门槛。
CUDA 仅具有共享函数标注，未编译或执行验证。

## 实际命令与结果
首次误选 production build-studio-cpu；BUILD_TESTING=OFF，unknown target，未执行测试，完整失败日志保留。
随后使用绑定同源码的既有 build-cpu / BUILD_TESTING=ON：
cmake --build build-cpu --target arch_jeans_diagnostics --parallel 4：PASS。
标准 Build 触发既有 CMake regeneration，并仅编译/链接新数学测试 target。
ctest --test-dir build-cpu -R '^jeans_diagnostics$' --output-on-failure：1/1 PASS。
独立参考生成及 git diff --check：PASS。
未编译生产 ARCH，未运行 simulation/Preview，未生成科学输出。
没有前端/Host 改动，不重复已通过且不受影响的 260 项 Studio/Host 回归。

## 后续
一般 EOS/父态/科学预算批准后，复用这个数值函数接接受态 EOS 和 GridMetrics，再实现 flag/粗化/宏步生命周期/失败诊断与输出身份；不能因叶函数通过直接标记 JENS supported。
本地完整日志留在 studio/.local/integration/jeans-numeric-leaf-20261003/；无 raw scientific data 上传。
