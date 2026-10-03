# Plotfile resolved EOS 组分来源补充

## 位置与范围

联合计划 3C 后的独立 plt 小切片。源码基线 6c10cb2a0f0897e157db8e19221414c8b40088a9，
本次修改 writer 来源接线；不改科学公式、计算场数组或 checkpoint v6。
真实 CPU 构建来源为该 HEAD + Summary.json 中明确列出的 dirty source/input SHA；
不能称为 clean HEAD-only build。binary SHA-256：
325ef7806af16e85c6dbfc545937da5e5bab32115e57ed5167a788296be4f91b。

此前 SourceIdentity 只存 species names。
runtime CheckpointProvenance 已有实际 A/Z/gamma/Cv；缺少它们时，
“同 EOS policy/table/names”不等于完整 EOS 定义相同。
本轮直接复制 authoritative runtime provenance 的四个 vectors，
不从 UI/schema default/文件名回填，不构造新的科学默认值。

## Additive 字段与映射

位于 /SourceIdentity，仍为 candidate-identity-1 / scope=partial。
新增属性：

- species_properties_version=checkpoint-species-1
- species_properties_state=recorded 或 unknown
- species_properties_source=resolved-runtime-checkpoint-provenance 或 unknown
- unknown 时带 species_properties_reason

recorded 时新增四个 FP64 [Ns] dataset：
species_A / species_Z / species_gamma / species_Cv。
严格按 species_names 顺序；与 checkpoint
/Species/A、Z、gamma、Cv 同序、同值，不涉及 native cell 数组。
保留 raw 定义，不新增 property 单位推断或 Cv 换算标签。
完整单位/复合 EOS fingerprint 由科学 review 和读取适配约定。

旧 caller 若未提供四组值，保留 unknown/reason，不猜值。
只提供部分 vectors、长度不匹配或非有限来源值会在临时文件创建前拒绝，
已有正式文件保持不变。旧 H5 保持可读，不回写历史文件。
当前 Host/Viewer 忽略 additive 属性，仍只展示旧来源子集，
不声称新属性已经进入 UI Inspector 或完整身份校验。

## 验证

- CPU ARCH 既有 build-cpu 增量构建通过，没有新 configure。
- 指定 initial topology t=0 测试：Sod/Cellular 两个子例通过，time=0/step=0。
- 真实 Sod Ns=1、Cellular Ns=19，
  四个 property arrays 对 checkpoint FP64 位级差异 0。
- 原有 DENS 全 native cell 对 checkpoint 位级差异 0；
  center/measure 最大差 0；Cellular bounds 差
  1.7763568394002505e-15 继续保留，无新增容差。
- 既有 Host/client metadata、slice、LOD、point 对新文件通过。
  最初传入 object rows 而非 verifier 要求的 path list 导致参数 ENOENT，
  改用 directories.json 后通过，没有修改 Reader 或绕过 validation。
- publication/checkpoint compatibility 两项通过，0.19s；
  包含新属性读回、缺失 caller unknown、partial/nonfinite 拒绝。
- HDF writer 已变，因此重新运行真实 Driver 的五类失败和编号保留：
  write/flush/close/rename/create 全部通过。

## 复现及交付

扩展现有 validation/io/verify_initial_plotfile.py，
recorded 状态下核对 version/source、FP64/shape、有限值与 checkpoint uint64 bits。
输出明确区分 policy/table/names 子范围和新 constituent 对照；
不把任何子范围标成完整 scientific certification。
原始 H5/plt/checkpoint/ELF/logs 保持本机
studio/.local/integration/plotfile-eos-properties-20261003 及 amr-production-oracle。
提交工具、源码和处理后摘要；没有上传原始数组。

## 未完成

整体来源仍 partial：run/effective-config/build/sourceGit 等未完整记录，
完整 EOS composite/freshness 仍未定义或核验。
本次不是 EOS 独立物理参考、演化、全域 AMR 覆盖、大文件或 CUDA 验收。
继续按具体 finding 收敛 Reader 适配与来源身份；完整联合目标保持进行中。
