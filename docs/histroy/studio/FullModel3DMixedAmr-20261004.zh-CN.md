# 三维初始混合 AMR：有界 CLI 与显示数学补证

## 真实身份与范围

基线 e267fdfcfba205b23dfe9a075b351f29fcdd10a8，开始 clean。
复用现有 CPU binary 325ef7806af16e85c6dbfc545937da5e5bab32115e57ed5167a788296be4f91b，
构建输入身份见 PlotfileEosConstituents-20261003.Summary.json；不把当前 HEAD 当作编译来源。
没有重编 ARCH、simulation、scientific output、checkpoint、CUDA 或 Windows 工作。

已有全模型 Host/desktop 证据仍有效，但尚无三维 mixed refinement 代表。
本轮直接 authoritative --preview-amr；请求 stdin 未自动 Save 原项目 .par。
全部请求 mesh-max-blocks=512 / mesh-memory-mib=256，未随 limited 自动增加预算。
复用维护的 RT 物理参数、AMR 阈值，仅改根块布局/激活第三轴；
这是新工程输入，不声称二维/三维物理等价或冻结演化 benchmark。

## 正负结果均保留

1. 首次 Sedov 调用错误加入 CLI 不支持的 --config-revision，exit2/INVALID_REQUEST；
   不算接口通过。核对实际 parser 后仅移除错误参数。
2. Sedov 2×2×2 / z extent=2 代表返回 complete=true、8 个 L0 根块；
   没有 mixed refinement，不能用于声称混合细化通过。
3. RT 1×4×1 返回 limited、4 个 L0，completedPasses=0，
   workingCapacity=18、regrid-exceeds-working-capacity。
4. 另一独立 RT 1×3×1 输入返回 limited、10 个叶块：
   L0=2 / L1=8，completedPasses=1，下一轮容量不足。
   last-completed-balanced-hierarchy 不等于最终 complete；保留未完成状态。
5. 小型可提交输入 src/api/examples/local-workflow/rt-3d-limited-amr.par
   仅增加 init-only 注释、规范行末空白和修正旧 x3 注释；
   raw SHA339289de3d8d979c985d5a25c0a2c1fb5c3c3b1e7bde82ad3e52a296e35966b1。
   新请求 revision 等于此实际 byte digest，mesh data 与前述三根块输入完全一致。

初次手算误假定 z extent=1，错误得到 expected volume=1/4；
外部 RT 输入实际 x3_max=0.25，正式 verifier 从外部文本独立解析，
精确 domain volume 为 1/16。没有修改 Core、几何或容差来消除这个测试假设错误。

## 独立 topology 与 Studio 接入证据

verify_initial_amr_topology.py：
以 exact Fraction 对存储 bounds 和外部 numeric Cartesian domain 检查。
无重叠、20 个共面相邻 pair、无不满足 2:1 的共面 pair，
stored leaf volume 与输入 domain 均为 1/16；level counts 与实际 leaves 一致。
遗漏 leaf、重复重叠 leaf、失衡 level、错误输入 digest 四个反例明确拒绝。
这是 Cartesian box partition 检查，不是一般曲线测度、场值或演化参考；
遇到非精确覆盖不能自动加容差，交 owner 审阅。

verify_initial_amr_display.mjs：
使用现有 validateWorkflowCore；expected case/request 独立指定、revision 来自输入 bytes。
三轴所有测试切面，包括 native内/外边界，81 次实际 leaf center 命中均一致；
隐藏 level 不可命中，原 mesh 对象未变。
AMR 仍无 cell field arrays；不能将 Init samples 当 AMR cell values。
这是当前 validator / slice / Inspector数学证据，不是实际 canvas/desktop UAT。

三份合法请求工作目录前后均无文件，execution 明确 timeStepping=not_executed、
scientificOutput=not_created、simulationReadiness=not_checked，binary digest 未变。
原始 JSON/stdout/stderr及不同输入留 studio/.local/integration/full-model-3d-mixed-20261004；
提交输入、脚本与处理后摘要，不提交全量 hierarchy/fields。

## 文档同步与复现

INITIAL_AMR_API.md 原“仅 Sod/Cellular”及所有 bounds/spacing=cm 已过时，
现按所选 binary runtime discovery、原生 coordinates metadata、逐轴 cm/rad 和三维切面更新。
当前二维 cylindrical 仍是极平面，不把它改称 RZ；注册不等于所有配置可运行。
本轮不增加 capability、protocol 或实现代码。

从具有相应接口的 CPU binary 执行：
    ARCH --preview-amr RT --config-stdin --request-id mixed3d-rt-canonical-20261004 \
      --mesh-max-blocks 512 --mesh-memory-mib 256 < src/api/examples/local-workflow/rt-3d-limited-amr.par
将 stdout 保存在本地，然后：
    python3 validation/io/verify_initial_amr_topology.py --response LOCAL_JSON \
      --input src/api/examples/local-workflow/rt-3d-limited-amr.par \
      --case RT --request-id mixed3d-rt-canonical-20261004
    node validation/io/verify_initial_amr_display.mjs LOCAL_JSON \
      src/api/examples/local-workflow/rt-3d-limited-amr.par RT mixed3d-rt-canonical-20261004

脚本语法、真实请求、正负 topology、当前 TS validator/显示数学及 diff-check PASS。
生产源码未修改，因此不重复先前316项检查，也不把旧PASS冒充本轮新回归。
native wheel/zoom/pan/fit、三维 mixed canvas/field overlay、完整科学 review、
Jeans/RZ、CUDA与冻结性能/长时目标仍未完成。本补证不封箱全模型或联合目标。
