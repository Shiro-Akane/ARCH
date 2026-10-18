# RZ 接触对数主项可靠积分节点（2026-10-05）

## 结论

SCOPED CPU PASS；STRICT CONTACT NOT COMPLETE；production RZ gate 保留。
已实现接触奇性对数主项的解析积分，减少可靠界的过度估计。固定16384矩形、
原内部relative target=1e-10、absolute target=0下，三个 matched 样本仍真实
返回 WorkLimit。没有放宽科学阈值、设置 floor、softening 或静默宣布收敛。

## 原审计及动机

上游源码 baseline d69c75c7cd1e3d44af240b3ae2f70cd1dbd406d5，unique workspace
ARCH-compute-optim。原 non-contact Gauss 已缩紧源外 bounds；接触 audit 使用
同一完整源 r=[0.5,1]、z=[-0.375,0.375]、rho=1、shared CGS G：
observer (1,0)、(1,0.375)、(0.75,0)，分别覆盖真实外面接触、corner、inside。
原始/处理后的 local audit 保留原 WorkLimit，不当作科学签收。

## 数学依据

[NIST DLMF 19.12.1/.3](https://dlmf.nist.gov/19.12) 给出 complementary root
q 的 K 收敛级数。其系数 c_0=1、0<c_n<=1，digamma差满足
0<d_n<=d_0=log(4)。因此对于0<q<1：

    L=log(4/q) <= K(q) <= L/(1-q^2)

这里 q=k'，不是模数 k 或参数 m。本节点依据该级数推导上下界，而非用
asymptotic O(q²) 冒充可靠数值常量。q=0的接触点有零测度，奇性由有限积分处理。

h=r*K/s；在接触矩形上以可靠 r/s 范围、s范围和 q_upper<1 围住
∫log(4s/d)。每个 observer quadrant 的 log distance integral 精确为：

    I(a,b) = ab*(log(sqrt(a²+b²))-3/2)
             + (a²*atan(b/a)+b²*atan(a/b))/2

于是 quadrant 的 main part 为 area*log(4s)-I；两个 dimensionful log
仅作为上述相消表达式内部部件，最终为无量纲距离比的积分，不引入新物理单位。
a=0或b=0按零面积几何消去，不采奇点、不加epsilon。

log 用现有 enclosed positive series/倒数；atan 使用半角缩减与60项交错级数，
包含明确 next-term remainder。节点、width、ratio、pi、基本运算、primitive
组合均 outward enclosure，不依赖 libm atan/log 的未给误差假设。

contact interval 与原 logarithmic majorant 取交集；无可靠表达时仍走旧界，
不相交明确失败；未知 q>=1 范围不硬套余项。没有改变完整源、观察点、G、
native geometry 或势点值语义。

## 证据与边界

新增 ring-contact-log contract / primitive probes；20个高精度primitive检查
覆盖atan、positive log及quadrant primitive，并用独立Decimal reference的
不同缩减形式检查算术。独立reference的quadrant闭合式主要证明算术，
不能单独视为物理独立积分验收。

3个小接触矩形 matched samples 使用独立 Duffy source triangle映射、
24/32阶、Decimal 80/100位 AGM诊断。工具不导入生产 K/interval/primitives；
诊断值在可靠界内。Duffy order差仍只是诊断，不当作quadrature可靠余项。
inside、edge、corner均无奇点采样。

固定16384-work budget下，旧/新3个全源接触势误差上界分别为：

| observer | old cm²/s² | new cm²/s² | 状态 |
| --- | --- | --- | --- |
| (1,0) | 9.5021919175e-15 | 9.1198984728e-15 | WorkLimit |
| (1,0.375) | 1.6719410233e-15 | 1.6069572115e-15 | WorkLimit |
| (0.75,0) | 4.3659014235e-14 | 4.0532272711e-14 | WorkLimit |

约4%～7%的缩紧只是实现效果，不是据此产生新验收门槛。全部仍超出
原1e-10相对目标；source数组、full kernel work counters和失败状态见summary。

最终 ring-contact-log、ring-separated-gauss、ring-enclosure、ring-native-face、
ring-far-leaf、ring-axis-enclosure、boundary-ledger、4项CTest及architecture audit PASS。
git diff --check PASS。原失败audit被保留，未覆盖成成功。

## 构建与原始数据

CPU Release现有build tree，仅增量ARCH、arch_composite_poisson、arch_self_gravity、
arch_gravity_stage_contract，无configure/新树/新workspace。memory guard未停止、
swap增长0。构建时本节点source dirty，文件hash/实际scoped ELF身份见summary。

production ARCH ELF仍为 d83186386a3dbcb403f473426da7dd42d1fc42840b09f7ddc43a7977fb36d95e，
保持内部gated path；匹配的JENS 9短演化+9真实重启receipt不重复执行。
新helper经本次scoped test ELF验证，不误称已启用production RZ。

原始H5、plt、checkpoint及日志本机留存：
studio/.local/integration/rz-contact-budget-20261005。
提交处理后的标量摘要、独立reference脚本和数学依据，不上传原始数据。

复现：

    build-cpu/arch_composite_poisson ring-contact-log
    python3 validation/gravity/rz_ring_contact_log_reference.py --probe build-cpu/arch_composite_poisson --output <local-output.json>

## 后续依赖

接触周围的非接触矩形仍占较大误差预算，下一步提高同一共享规则的可靠积分阶数/
接触余项控制；严格目标必须由真实工作量和成功状态证明。general-parent
far/translation、完整coefficient/assembly/residual ledger、真实AMR source
publication和角动量全消费者仍未关闭。RZ-VISC-01/RZ-AXIS-01参考待审保持独立。
没有开放production RZ、启动对应CUDA/长跑或Windows适配，不修改tag。
