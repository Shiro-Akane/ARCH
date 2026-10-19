# O7.1 JENS 冻结 CPU 短包节点

Core uniform-lifecycle-1：4d9f38ac89a54c7bf57828af1d423401a4bb6167；
最新科学决定：86bec324018349c6d81df84a3cced3ed9f9a2792 第6–8节。
本节点为有界短科学 gate，不代表完整 O7.1 / RZ / CUDA 验收。

## 实施与身份

公开 JENS refine_var 和输出只允许 self gravity + explicit CPU；CUDA/auto 未获得资格，不静默禁用或替换。
jeans_cells 是无默认的条件输入，JENS 选择时必须有限且 >=4；output-only 不强制引入目标。
API inspect-config/config-schema 与 Runtime 使用同一实际条件。Studio field Preview 未宣称有 JENS 场支持。

运行源码基线 ab63f4bfe06e29ef7314934affe55b84b88fef77 + 本提交接线补丁；
repositoryDirtyAtRun=true，真实 ELF SHA256
ab7b5ee50b94e8516fe378c312814b281d3ec6c0aed68fee1831b17bf87a263a。
各输入/源码与最终交付文件 fingerprint 见 summary.json，不把 dirty 源码冒充旧 commit 的 binary。
没有更换 Studio managed binary 或其 Build Manifest。

## 结果

Cartesian GravityBox、单 IdealGas、rho=1e7、T=1、periodic；
冻结 t=0.02，checkpoint t=0.01，target=160，lmax=1，capacity=64。
1D/2D/3D 各 disabled/output-only/active：9组 PASS，9组连续与续算 checkpoint
全部属性及顶层数据集精确一致。主动细化为8/16/32叶块，128/4096/131072 cells。
DENS/PRES/TEMP/ENER/gas 保持冻结常量，原始速度、Phi、活动轴 gravity 均精确零。
mass 与 gas-energy drift 均0，repair events=0；原重力 residual/lease checks 通过。
JENS 独立 Decimal120 静态参考最大相对误差4.50750606346323e-17，小于16epsilon界。
disabled 与 output-only 共有原生场、网格、checkpoint、solve/stage counts 精确一致；
单次 wall time 仅记录，不据此宣称性能提升或开销阈值验收。

active 每个接受宏步均有实际 regrid transaction 记录：含初始化共29/55/83个步号。
NJ>=160在输出态检查，运行时同一真实EOS hook在每个接受态 enforce；
不将3个输出快照说成独立逐步数值采样。
CPU fixture另验证 target64父态合并、target160拒绝、容量不足的事务回滚。
真实3D max-level=0和max_blocks=32分别exit1；无H5/plot/checkpoint，
均在advance前拒绝。日志中的 Simulation Started 是进入driver提示，不是完成一步。

## 检查、失败留存与复现

configuration_api_contract、configuration_input、jeans_diagnostics、
configuration_entry_contract、configuration_v3_contract、
runtime_validation_inputs_contract、refinement_indicator_math、
plotfile_publication、checkpoint_compatibility、真实源树架构审计和diff check PASS。
旧配置测试94项及静默过滤JENS假设已同步95项和明确拒绝语义，不改阈值。

第一轮3D读取工具缺 NativeGrid 而失败，原始失败JSON及全量日志保留本机。
修复仅从原有 Grid/x,y,z 的精确规则格点恢复物理间距，验证数组方向及顺序；
没有新增3D writer/Viewer或根据config猜测网格。修复后3D三通道及续算PASS。
此前1D/2D通过记录复用，没有无变化重复演化。
只读汇总工具 validation/gravity/check_jeans_uniform.py 接受第一轮 result.json 和修复3D result.json；
实际演化复用 BoxCampaign.jeans_uniform_lifecycle，不另建CI或初始化器。
原始目录见 summary.json；H5、plt、checkpoint、ELF和全量日志均不提交。

## 余下门槛与RZ顺序

非均匀 JeansWave、其他EOS/低密度、关闭路径性能和CUDA仍待对应冻结验收。
只关闭uniform-lifecycle-1短包，不开放完整RZ；本节点不启动新RZ长跑。
RZ按Core第8节A→B→C→D，唯一m_phi=J/W，保留V/W区分；
旧V平均RZ静态父态PASS不能用于新表示，必须重算。
有限环体数学按第7节可独立推进；近场误差估计不得标为certified bound。
CPU相关消费者和科学gate通过后统一CUDA，再按已冻结模型启动长跑/计时。
