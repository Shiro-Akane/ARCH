# Sod 演化与 Restart output-session 验证

## 范围与准确身份

本轮沿用此前 3C 固定网格 Sod 的输入：tmax=0.2，checkpoint 间隔0.05，
分割点0.05/step67，终点0.2/step280。仅替换本地 output directory；
restart_file 指向本轮生成的同分割点 checkpoint。没有重新选物理参数或 O9 终点。

实际 CPU binary 编译源码869ae3d3d30e8a9b16e377112af73e737b6c736a，
SHA f82bb7ff16c4acf54ae84970b9b403dce3d0370a468d241169519b3bfd1f6a44，
7400456 bytes，OMP_NUM_THREADS=1。验证基线734d6ebc；仅添加验证脚本，
没有修改生产源码、场数组、单位或 checkpoint 格式。构建完整身份见 CpuRzSourceRebuild-20261004。

冻结输入来自795e6be7-105e-4b3d-a48a-eae68a6f4303与
30db4e5c-7c43-4cac-8b30-9af8fad393db的本地 input.par；
准确 SHA、新输入、分割 checkpoint、输出 fingerprint 和 session ID 见同名 Summary.json。

## 检查结果

- 连续与续算均正常退出，终点0.2/step280。
- 最终 checkpoint 21个 dataset 一致；20个数值 dataset dtype、shape、
  完整原始字节一致，species string 一致；metadata一致。
- 与旧3C终点 checkpoint 的21个 dataset一致，原有metadata一致。
  新增独立 geometry_semantics_revision=1 / geometry_chart=existing，
  checkpoint_version仍6，不把旧缺失身份标为RZ。
- 最终四个 Plotfile字段DENS/PRES/VELX/ENER，shape=[8,16]，FP64完整字节一致。
- 连续5个Plotfile使用同一UUIDv4；续算3个Plotfile使用另一UUIDv4。
  每份文件case_id=Sod、binary_sha256与实际ELF一致、
  raw_config_sha256与本次parser输入一致。
- 续算读取的checkpoint、冻结输入和旧参考checkpoint SHA前后不变。
- 原始H5/plt/checkpoint和stdout/stderr保存在本机ignored studio/.local；
  只提交验证脚本与处理后摘要。

## 限制

这是已存在固定网格Sod的CPU演化/Restart工程回归，不是新增native desktop UAT，
不证明独立PRES/TEMP科学精度、不覆盖二维演化AMR、CUDA、RZ或O9长期演化。
本轮没有重新验证此前通过的发布故障注入；也不新增真实ENOSPC、fsync或断电承诺。
尚未知的build/effective-config/source身份仍按原writer语义unknown，并保留原因。

复现入口：validation/io/verify_sod_restart_output_identity.py；
传入project、binary和不存在的ignored output-root，
在含NumPy/h5py的本地环境运行。脚本先核对冻结输入及参考checkpoint SHA，
再顺序运行和比较，失败直接抛出并保留本地输出。
