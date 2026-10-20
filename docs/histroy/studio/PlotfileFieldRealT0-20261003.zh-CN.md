# Plotfile 真实字段声明与读取接入

writer 基线 ab23bad7e8bea93c3569618d1a6335312372cc30，构建前工作树 clean。
CPU Release / CUDA OFF，复用本 worktree 的 build-cpu，8 jobs 增量构建约20.32秒。
实际 binary SHA c294f0d1be009bdeca36174a00a28c6ff6ca48b11b499416f6556a2ecb6b4bec；
本机持久 ARCH-field-t0 副本独立保留，未替换桌面当前 selected binary。
source commit 不等于 build freshness 的完整证明，原依赖覆盖缺口未宣称关闭。

## 授权的实际输出

精确复用 Expansion.test_mesh_matches_production_initial_topology 的 Sod/CellularDet t=0 方法，
原 EOS、网格、refine 阈值和 max_blocks；tmax=0/max_steps=-1，无时间步演化。
新 output 目录与旧证据分离，绝不覆盖旧参考 H5。
方法1/1通过（两个 subcase）；checkpoint time=0/step=0。
raw config digest 含唯一 out_dir，因此不能要求新旧 raw config SHA 相等。

独立 verify_initial_plotfile.py：Sod192、Cellular5120个 DENS 与 checkpoint uint64
bit-pattern mismatch 都为0；Plot/CHK/Preview logical keys相同。
中心/测度最大差0；Sod bounds差0；
Cellular per-cell bounds最大差1.7763568394002505e-15仍保持科学 review finding，
不加新容差、不以相同 block extent替代这条 finding。

## Host、客户端与界面实现

原 Host 固定要求 eos_unit_system/measure_unit=unknown；它拒绝真实新文件，
因此迁移成 bounded recorded declaration，而不是 reader 按字段名猜单位。
旧文件缺少 declaration 时字段单位仍 null/unknown；不 retroactively补单位。
新 candidate-field-1 类型验证版本、cell centering、unit/basis/meaning和unknown reason。
保留 partial source identity，不允许猜 run/effective/build/source Git。
CGS 记录支持 cgs，其他 unit-system明确拒绝；已知 coordinate/time unit要求匹配的CGS证据。
native measure unit/normalization按1D/2D交叉检查；slice必须与header保持相同标签。
raw payload unit必须匹配所选字段声明，不能伪装成其它单位。

Plot axes、色标、raw table、Native Inspector及source evidence显示这些记录：
density g/cm^3、coordinate cm、time s；measure cm或cm^2及低维归一化；
field meaning/basis/unknown reason供 Inspector回查。
数值、Config、Preview、LOD reduction和checkpoint未改变。
unit标签不提升completion/renderEligible或scientificIdentity为已认证。

## 已执行验证

- 真实新H5经Host metadata/slice/overview/point及client validator通过；
  DENS、coordinate/time、sourceCGS、measure normalization均匹配。
- Sod物理点.49返回raw DENS=.125/index186；
  Cellular点[.5,6.5]返回43375362.843074/index3074；
  LOD扫描192/5120单元，明确与原生值分开。
- 309项Studio/Host回归、lint/typecheck/build通过；diff check通过。
- 新HDF反例涵盖unknown单位原因、未声明legacy字段、版本/centering、
  大字符串、payload单位错配和低维归一化；既有原始/非有限值与来源测试仍通过。
- production assets index-BJQ3bLmF.js已生成，但本轮尚未对新单位显示完成native桌面UAT；
  不将Host/SSR/编译结果算作实际用户操作。
- Vite已有大chunk非阻断提示保留，没有借此重构打包。

可复现只读入口：validation/io/verify_plotfile_reader.mjs，参数为本机evidence目录JSON。
原生数组/日志/H5/plt/checkpoint/executable留studio/.local；
tracked Summary仅身份、检查结果和点选/差异汇总。
旧writer与新writer引用分别记录，不把当前HEAD冒充旧文件binary来源。

## 仍未完成

新单位native UAT、全字段生产验证、完整run/effective/build/source身份、维护者单位/basis审查、
全域AMR完整性、大文件/缓存/索引/同机运行影响继续待办。
此项不解除O7 Jeans/RZ科学参考/预算及第二平台CPU/CUDA/O9待验收项。
无push/tag/main merge/Windows适配；完整联合目标继续未完成。
