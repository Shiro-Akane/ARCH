# O7.1 JENS：Core／Host／Studio 静态消费补证

基线7f2f8e34da1e959bdee78ee80d445607096be76e，开始工作树clean。
本次处于联合计划第6项O7.1的消费者补证；第7项RZ数学和科学门槛继续推进，
不是整阶段完成或native desktop UAT结论。

## 状态重核与实际迁移

已重新fetch约定review refs：
origin/codex/o8-boundaries仍11a321d56，integration与本地7f2f8e34相同；
没有新应力/轴区科学决定，没有merge/reset/stash。
3B/3C历史工程出口沿Studio3CExitAudit及所列精确身份，不重复不变工作。
完整模型/Plotfile的已有成果仍按各报告来源保留，不凭文件存在宣布全部完成。

发现并修正两个消费者残留：
1. CONFIGURATION_API仍写JENS pending/unavailable，与已通过CPU frozen
   uniform-lifecycle-1、现行RefinementMetadata/Runtime条件不一致。
   现在说明self gravity + explicit CPU才可用，auto/CUDA拒绝，无fallback，
   不据此声称JENS Initial Preview/其他EOS/RZ已支持。
2. 既有真实configuration-core-v3.integration工具固定断言94参数，
   现在按实际唯一key集合核对并消费jeans_cells，不把95写成永久生产常量。
   静态工具改用production selected-binary入口，不制造“成功Core Build Manifest”
   给copied ELF配上假的源关联。Build/Preview readiness仍有原Host测试覆盖；
   此工具明确只验证静态selected executable。

没有新增Core/前端默认表、schema、条件解析器或CI矩阵。
真实ConfigurationAdapter、validateConfigurationSchema/Inspection、
catalog、parState、EditHistory共同执行；不是按测试重写UI规则。

## 实际PASS证据

当前CPU ARCH SHA256：
1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。
version3，实际95可用standard keys，新key无default、unit1、min4、AMR subgroup。
Sod在此仅用作static inspection fixture，不是新self-gravity科学算例。

- self+cpu、未选择JENS：available=true、selected=false；不是先选中才开放。
- 请求JENS而目标缺失：Core诊断；inputState=missing、parsedValue/source=null。
- 同一动态catalog可找到空白jeans_cells；未编辑前原文逐字不变。
- 首次显式160只插入一个key；一次Undo恢复原文、Redo恢复一次编辑。
- Core读取160、source=input；原文本SHA、case、binary身份贯通。
- 输入3保留原文并显示range错误；不把不适用隐藏当作错误已解决。
- auto/CUDA的显式JENS请求拒绝，不替换indicator或默认为CPU。
- output-only不要求jeans_cells；缺值保持null，不从schema“补出”默认。
- valid/empty/重复坏行/response超限原覆盖仍保留。

Schema Default、Inspection Parsed和Preview Effective严格分开：
本次没有Preview，后者不能用160或schema值冒充。
整个检查不执行Setup、EOS加载、Preview、AMR、simulation或Core build；
Host身份是selected-binary:<SHA>，不是current Build claim。
隔离测试只临时复制该CPU ELF与测试文本，不复制源码/CMake cache/build tree；
过程后精确文件集合不变、untouched.par保持原字节，临时夹具按原入口清理。
实际ARCH工程唯一工作区未改managed binary/Build Manifest。

真实集成13个命名检查组PASS。
Studio/Host npm test 334/334、fail0/skip0，Host子集不重复相加；
lint/typecheck/production build、final git diff --check均PASS。
身份入口最后调整后重跑了受影响真实集成与lint，
没有无变化重复完整回归或JENS9+9科学短包。
现有>500kB bundle warning保留，不改阈值或顺手优化。

复现：
cd studio
node tests/configuration-core-v3.integration.ts ../build-cpu/bin/ARCH
npm test
npm run lint
npm run typecheck
npm run build

处理后身份/指标：
validation/backend/results/o7-jeans-studio-contract-20261005/summary.json。
raw/full logs仅studio/.local/integration/o7-jeans-studio-contract-20261004。
测试开始跨日，目录保持原名，不迁移或覆盖本机证据。
不上传H5/plt/checkpoint、ELF、全量日志/场数组。

## 未完成与下一项

本次不是GUI人工点击验收，不把catalog/Undo静态证据写成native window UAT。
当前JENS schema还需native窗口补证；其他EOS/低密度、非均匀JeansWave、
关闭路径成本与CUDA仍按原冻结预算和批准范围验收。
RZ仍需更紧contact界、far/tree/assembly/residual ledger、
RZ-AXIS-01和RZ-VISC-01决定及完整消费者科学签收。
下一工作优先补实际JENS窗口消费或上述无阻塞Core依赖；
不会因缺少单项科学决定暂停全部工程，不转向Windows。

CPU对应科学出口后统一CUDA；已冻结对应长跑/计时才按原协议启动。
仍需最终逐项核对配置、3B/3C、全模型/Plotfile、O7.1–O7.5、
CUDA/性能/批准O9和证据清单，整体目标保持未完成。
