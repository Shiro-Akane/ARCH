# Canonical Plotfile 真实 CPU t=0 读回

## 精确身份

源码与 clean build 来源：a2b0658bfe6d0bc36eb87983c5b8131327e817b3。
验证 binary SHA-256：b360c662cd51faf6d147ff6c7c7dfa932a0c1d98cd1967d345a920b9ad13991a。
使用既有 build-cpu 增量构建 ARCH，8 并发，exit 0，耗时 9.69s；没有新 configure。
Owner checker：23ff77c4f08419de2b3c5eadee214da2af25784e；仅读取使用，没有 merge。

## 实际文件与范围

| case | 字段数 | 每字段 shape | 文件 bytes | 文件 SHA-256 |
| --- | --- | --- | --- | --- |
| Sod | 9 | [12, 16] | 55376 | 7cbbf3d54bb96b3d1c04449cd18116062e6d8f645096ddc3c7dda481d91b9d38 |
| CellularDet | 28 | [20, 16, 16] | 1599616 | 986e316496087f786ca49e14779a7d46894e0cad59a3a8ffa80f2dbee619c937 |

只运行批准的 t=0 fixture；time=0、step=0，不覆盖后续演化验收。
ALL 输出仅修改参考配置的 plt_variables 和唯一 out_dir，其他科学输入保持。
字段完整名称、声明、min/max、输入 SHA、checkpoint SHA 与原始文件摘要见同名 Summary.json。
ENUC 未导出，因为参考配置未启用 burn；不为凑字段强行改配置。

## 验证结果

- t=0 initial topology：一项测试、Sod/CellularDet 两个子例通过。
- 全部 37 个导出字段通过 Host/client metadata、slice、LOD、point 查询。
  每字段选中点与 HDF 原始 FP64 位级差异为 0。
- DENS/ENER/组分 X 全原生数组对 checkpoint 位级差异为 0；
  VEL 对 authoritative momentum/rho 的逐单元结果位级差异为 0。
- DENS-only 与 ALL 的 rho、momentum、eng、enuc_rate、X、rhoX checkpoint 状态位级差异为 0。
  新旧 binary 的同组 t=0 checkpoint 状态也位级差异为 0。
- canonical ENTR 的真实文件声明为 pressure_density_proxy；
  unknown unit_reason 保留 P/rho^Gamma1 与 local EOS exponent 说明。
- Owner validate_header 对两文件的实际分散属性映射通过；
  外部可信 case/config/binary 逐项核对。
  这只证明 header 子范围，未运行 owner 完整 bounds/measure array adapter，
  不代表全部来源身份、全域 AMR 覆盖或物理精度验收。
- DENS native center/measure 最大差为 0；Sod bounds 差为 0。
  Cellular bounds 对 Preview 最大差为 1.7763568394002505e-15；
  如实保留给 owner review，没有新增容差。

## 复现入口与布局

validation/io/run_plotfile_fields_t0.py、
validation/io/verify_plotfile_fields.py、
validation/io/verify_initial_plotfile.py、
validation/io/verify_plotfile_reader.mjs --all-fields。
运行所需 approved reference inputs、binary 与原始 H5/checkpoint 都在本机持久 studio/.local/integration。
具体命令参数以各工具 --help 与本机 evidence-directories.json/runs.json 为准。

布局继续使用 PlotfileReaderAdapterHandoff-20261003.zh-CN.md：
Data/<field> 为 FP64 [B,Nx] / [B,Ny,Nx]，i 最快；
Grid 坐标和 NativeGrid bounds/measure 使用相同 C-order 展平；
level/Morton 按文件 block 顺序，不排序。
没有为 checker 增设重复 production JSON，也不改 checkpoint 数值/格式。

## 尚未关闭

完整 build/EOS/run/effective-config/source Git 身份仍未全部记录；
PRES/TEMP/ENTR/VORT/DIVV 尚需独立科学参考；
发布故障 scoped 证据不能替代真实磁盘耗尽/持久性或 Driver 输出序号完整验收。
首次总览仍可能全叶扫描；索引、缓存、大文件成本与同机影响需继续验证。
本次未修改 Studio UI，未重复无新改动的 Studio 全回归或旧 native UAT，
也未把此 binary 自动作为 Desktop 的 fresh selected binary。
原始 H5/plt/checkpoint/ELF/full arrays/logs 不提交。
