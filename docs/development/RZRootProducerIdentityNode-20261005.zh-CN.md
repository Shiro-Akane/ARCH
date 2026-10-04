# RZ actual ring producer root coordinate identity

**EXACT-COORDINATE PRODUCER SCOPE PASS；不是完整 RZ 科学签收。**

基线49f607252db4bf76c174c6a612122d96b811fc6c；唯一工作区/home/arch/projects/ARCH-compute-optim。
沿科学清单§7的真实source/observer identity依赖推进，不修改物理、源边界、观察点、积分、原请求或生产能力门槛。

## 真实数据路径

GravityBoundary::root_scoped_ring_errors(op,ring)消费实际ring_boundary返回值。
先执行既有require_current_ring，校验bound operator、density/source identity、topology和source generation；缺失face error array明确拒绝。
随后逐叶检查actual center±half-width两条轴的四个边界，以及每个exterior face.center的两条坐标。
理想位置是origin+(index+offset)*ldexp(root_spacing,-level)；source offset=0/1，face normal offset=side，tangential offset=1/2。

精确证明使用FMA乘积残差和TwoSum加法残差；任一步非零即拒绝scope提升。
root spacing/origin非零exponent限[-400,400]、level 0..15、|index|≤2^32且为半整数，保证最低有效乘积残差远高于subnormal下限，避免以残差下溢误判精确。
ldexp反向一致也必须成立。这是内部proof的保守支持域，不是物理精度参数或修改已有网格限制。
没有tolerance、近似相等、epsilon/floor，也没有用变更坐标重新积分。

所有source edge/observer coordinate必须精确一致，且原potential error为CertifiedAbsolute，才给exterior errors标记RootDyadicSourceAndObserver。
否则返回原absolute error/quality与Unknown scope，ideal B consumer继续拒绝。
原ring.errors不修改，原存储坐标结果不自动升级。exact-coordinate子集不等于general rounded-geometry certificate。
source tree父展开允许现有实际展开中心；原moment/translation enclosure针对同一leaf source，本次不另改父中心或矩。

## 独立证据

12组actual CPU producer：axis/off-axis、uniform/mixed、positive/zero density；264 native cells、176 exterior faces。
8组dyadic source/observer坐标逐项经独立Fraction验证精确一致并通过ideal B propagation；
4组origin=实际FP64 0.3存在非零exact-coordinate差异，保留Unknown并被ideal B拒绝。
参考从实际root FP64构造Fraction，不将文本0.3当作实数3/10。

rz_root_producer_reference.py不导入生产坐标证明或浮点helper，只从真实probe数组重建理想Fraction坐标。
额外合同检查：stale source generation、不同operator、missing errors、Estimate均不能获得root certificate。
原stored-RHS独立Fraction/100、140位source对照扩至同12组，全PASS。
原rtol=1e-10、atol=0保持；正源未求解数组仍拒绝，zero路径精确零，不据此宣称真实求解成功。

## 构建与检查

既有CPU Release tree，28并发增量；probe guard peak RSS464484 KiB，production guard peak1798828 KiB，swap growth=0，未触发guard。
6项Core scoped CTest全PASS，12.28s；architecture audit、diff check PASS。
production ARCH SHA256仍7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510。
匹配既有冻结JENS9短+9实际restart receipt，不重复相同ELF短包。
final probe SHA/source hash/receipt hash记录于处理后summary。

## 余项与发布边界

继续完成general rounded source/observer error producer，与native source、B construction、RHS assembly、A construction/evaluation和native RMS组合，执行原physical residual判据。
仍需真实solved Phi/force及完整RZ A→B→C→D、axis/viscosity/natural recovery科学检查。
production RZ gate保持；CPU相应科学gate后才统一CUDA及批准长跑/计时。不开展Windows适配。

复现（使用新的本地output root）：

    python3 validation/gravity/rz_ring_rhs_composition_reference.py --probe build-cpu/arch_composite_poisson --output-root <new local directory>
    python3 validation/gravity/rz_root_producer_reference.py --probe-record <directory>/probe.json --output <new summary file>

处理后证据：validation/gravity/results/rz-root-producer-20261005/summary.json。
raw density/source/rhs/geometry/potential数组、完整日志和ELF留在studio/.local/integration/rz-root-producer-20261005，不提交H5/plt/checkpoint。
