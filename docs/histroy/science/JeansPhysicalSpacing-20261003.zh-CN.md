# Jeans 诊断与活动物理网格尺度绑定

2026-10-03；基线 abdbf87d9b57c17e7e65eed7742e89ac09a38ff8。

## 当前实现
JeansDiagnostics::evaluate_cell 从既有 GridMetrics::PhysicalSpacing 读取每个活动方向，
选最大值后调用唯一 evaluate 数学叶函数。只接受 dimension 1/2/3 和每个活动间距的正有限值；
非活动方向不读取，不把负间距取 abs，不引入奇点 floor。
函数不选择 EOS、不缓存接受态、不设置 AMR flags、无生产消费者；JENS 仍 unavailable。

既有 2-D cylindrical/spherical polar、3-D cylindrical(r,z,phi) 与 spherical(r,theta,phi) 语义原样复用。
没有复制坐标公式、改变 GridMetrics，也没有用该工作提前声明 RZ 支持。
一般 EOS 定义、父态、条件必填和科学验收预算保持 pending；前序完整桌面矩阵仍未封箱。

## 独立检查
CPU 测试以已独立生成的单位输入参考除以人工明确的物理尺度，不使用 PhysicalSpacing 来构造预期值。
覆盖各向异性 Cartesian 1/2/3D、非活动 NaN/负值、2-D polar 弧长、3-D cylindrical 方位长度、
3-D spherical 赤道/近极区、首径向单元、非法 dimension、活动轴0/负/Inf/NaN与 unsupported geometry。
最大值规则独立覆盖：min(dx)、块宽度或体积尺度不能通过这些参考。
工程舍入界限沿上一叶函数测试，不定义新的 EOS、AMR 或轨迹科学预算。

命令：
cmake --build build-cpu --target arch_jeans_diagnostics arch_curvilinear_metrics --parallel 4
ctest --test-dir build-cpu -R '^(jeans_diagnostics|curvilinear_metrics)$' --output-on-failure

实际仅两个 scoped targets 编译/链接；CTest 2/2 PASS（0.12s），git diff --check PASS。
未重编生产 ARCH；无 simulation/Preview/CUDA/科学输出。Studio/Host 未改，不重跑不受影响回归。
本地完整日志在 studio/.local/integration/jeans-spacing-leaf-20261003/。

## 下一步
已准备共享公式和真实活动物理尺度；接受态/EOS、粗化父态及阈值科学规则批准后才接生产生命周期。
完整 O7.1 实现、科学签收与 CUDA 验证均未完成。
