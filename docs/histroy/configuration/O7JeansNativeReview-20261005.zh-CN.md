# O7.1 冻结 JENS 短包：Linux 原生消费者 review 节点

## 范围与依据

Core 科学清单 O7JeansRzReviewQuestions-20261004 第6–8节，
决定86bec324018349c6d81df84a3cced3ed9f9a2792。
fetch后 codex/o8-boundaries 仍11a321d5604f9ee62b9f9587c81f14de4f128bc4；
未merge main、重建分支或复制源码/build tree。唯一源工作区ARCH-compute-optim。
基线7bd2e80b06e36799c9bbaedaeded5fc33710bf5f；本次UAT包含本提交的Host profile修改，
sourceDirtyAtUat=true。14700K + RTX4070Ti；Linux/WSL优先，无Windows适配。

## 冻结短科学包的真实身份

当前CPU ELF SHA256：
1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。
00db019149962011f29f12917740ed2b3584eeaa节点已对该ELF完成uniform-lifecycle-1：
1D/2D/3D × disabled/output_only/active，共9次演化、9次实际checkpoint续算，
t=0.01→0.02；质量和气体能量相对漂移均0，三通道solve计数按原摘要。
准确处理后receipt为validation/amr/results/rz-amr-torque-runtime-20261004/summary.json。
初次短包的更早ELF身份及静态参考保留在O7JeansUniformLifecycle报告，不混写成当前新测。

当前ELF未变，相关Core实现无新改动，因此本节点不重复该短科学包。
条件目标、三通道、父态、容量失败、checkpoint和原生Plotfile证据仍按各既有报告。
7bd2e80b另有真实Core/Host/catalog/history消费补证。本节点补原生Linux窗口，
不是新非均匀演化、其他EOS、完整JENS、RZ、CUDA或性能签收。

## 原生 finding、最小接线与真实 Build 语义

第一次用现有build-cpu/bin/ARCH打开Linux窗口，在launcher被拒绝：
No approved Host-owned Build Profile matches this project/binary。
未进入参数页、未执行科学任务；错误原始日志留本机。
修正只注册Host-owned固定arch-compute-optim-existing-cpu profile：
source=/home/arch/projects/ARCH-compute-optim，
build=/home/arch/projects/ARCH-compute-optim/build-cpu，
target=ARCH，output=build-cpu/bin/ARCH，parallelism=28。
完整逐文件tracked集合继承现有review profile，替换cache路径并补GravityBox/JENS文件。
现有cache为Ninja/Release/CUDA OFF，两项绝对binding通过现有validateProfile检查；
错误source root拒绝。dependenciesComplete=false，绝不声称全依赖已覆盖。

未configure、未修改cache、未新建/迁移tree、未执行Core build。
没有制造成功Build Manifest。原有local Configure profile保持原身份。
新固定profile不注册Preview profile；没有成功Build时初始化/AMR仍不可用，
界面明确selected binary only / current tracked-input Build validation unavailable，
不能把本次schema读取当作current Build或Preview资格。
后续真实Build/Preview仍须走既有受管身份机制，不在本节点隐式开放。

## 原生 UAT

生产React assets + Linux Electron + owned Node Host，非Vite/浏览器替代。
窗口13306798，GravityBox，输入为冻结1D active input.par的本机逐字副本。
搜索jeans_cells并聚焦，显示160.0、unit 1、required when refine_var requests JENS、
No implicit default；Inspector分别显示Working Copy 160.0、Saved160.0、
Schema Default unavailable、Inspection Parsed Value160。

输入3并blur：原文保持3，出现Value must be >=4；Working Copy变为unsaved。
一次Ctrl+Z恢复160.0，dirty清除；等待真实Core inspection恢复160，
错误提示消失。没有使用schema/default充当model-read value。
未点击Save、Build、Configure、Run、Restart、Preview或AMR。
新窗口显示No real preview generated，模型能力与Host readiness分开表达。

正常关闭窗口，8个本轮owned PID/startTicks全部消失；
输入副本SHA256与启动前相同，ELF不变，不杀既有其他ARCH终端。
本次只签收上述参数编辑/检查/Undo/关闭，不宣称native缺值插入、
CUDA选择或所有模型UAT；这些条件的静态消费覆盖沿7bd2e80b。

## 检查与后续

注册profile后Studio/Host npm test 334/334，fail0/skip0，
lint/typecheck/production build PASS；profile绑定/错误source拒绝PASS。
Vite已有>500kB警告保持，不改阈值或顺手拆包。
没有因文档或同一binary再次运行Core/JENS baseline。

本节点交review：CPU uniform-lifecycle-1证据与Core/Host/Studio原生消费者连接，
不能据此解除RZ生产能力门槛。
环体数学第7节可继续独立推进；区分有限界、收敛与势/力误差。
RZ继续A→B→C→D；粘性应力和轴区norm的开放finding按精确报告等待Core决定，
其他已明确消费链可以推进。全CPU科学消费者通过后统一CUDA，再按冻结终点长跑/计时。

本机证据studio/.local/integration/o7-jeans-native-20261005；
处理后validation/backend/results/o7-jeans-native-20261005/summary.json。
原始H5/plt/checkpoint、ELF、全量日志与截图均不提交。
