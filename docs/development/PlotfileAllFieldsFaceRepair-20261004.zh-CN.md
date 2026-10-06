# 修复后 Plotfile 全字段与生产读取补证

基线 5e20f91d0d23c905089fd597856aa3545aeb91fc；复用从 clean b26fb8a2 编译的 CPU binary 1bdd71ed344f01dd9722898479d37780e81a834f4d0bdea501b23a933804e1a8。本轮没有再次构建 Core 或替换桌面 binary。

## 实际验证

用现有 run_plotfile_fields_t0.py 从上一轮明确 CPU tmax=0 输入产生独立 ALL 输出，仅改变 plt_variables 和 out_dir。两次退出0，checkpoint time0/step0；没有演化。

| 对象 | Sod | CellularDet |
|---|---|---|
| 字段数 | 9 | 28 |
| cells / shape | 192 / [12,16] | 5120 / [20,16,16] |
| 新 Plotfile bytes | 56792 | 1601016 |
| 与早期 canonical 全数组 bit mismatch | 全部0 | 全部0 |
| 新文件 native 全域 coverage gap/overlap/outside | 全部0 | 全部0 |
| production isolated raw point checks | 9/9 | 28/28 |

新 output SHA：
- Sod fe089e72113896ef7261064596ffdbf25cced4366ceaa84812dc2a5b102a8e9f。
- Cellular 81431b1d80407949d852381ab67b6d11dd4598090c6abe1963b882ce9fa2b3ed。

早期 canonical binary b360c662 和本次 binary 的身份分别保留；完整 source/input/output 字节身份见 Summary，不用当前 Git HEAD 代替编译来源。

## 链路与不变量

1. verify_plotfile_reader.mjs --all-fields 对37字段执行 Host/client slice、LOD、point，并验证记录单位/centering/basis、ENTR unknown reason、species 四组属性来源。
2. verify_plotfile_fields.py 按真实 logical block keys 与 checkpoint 对照；rho/momentum/energy/组分等checkpoint原值与上一轮 DENS 输入一致。DENS/ENER/velocity/species 原值回查位级差0，Host原生点值与独立HDF逐位一致。
3. 新 compare_plotfile_fields.py 使用外部 recorded run manifests 核对case/raw input/binary。新旧实际参数只有 out_dir 不同；要求同 field set/FP64 shape/块顺序，再比较每一字段完整数组原始bits，37字段全0。没有Morton排序后偷换索引。
4. 新 verify_plotfile_isolated_fields.mjs 通过 production worker 对每个字段各做一次实际点查，client严格校验file/request身份；原值与已独立HDF核对的期望精确一致（含Object.is符号语义）。
5. 新全字段文件重新运行完整 stored bounds coverage；不是引用前一份 DENS 文件替代。两个file严格通过。
6. 三项 checker 反例测试检出正负零、1 ULP差异、物理参数变化、块顺序变化。临时H5不提交。
7. 所有读回后 source hashes 不变，所有 raw files/arrays/checkpoint/ELF 留在本机 ignored。

运行工具：
python validation/io/compare_plotfile_fields.py --current-runs <new-runs.json> --prior-runs <canonical-runs.json> --output <local-summary.json>
node validation/io/verify_plotfile_isolated_fields.mjs <new-runs.json> <independently-checked-reader.json>
python -m unittest discover -s tests/tooling/validation -p test_plotfile_field_comparison.py -v

本机 evidence：studio/.local/integration/plotfile-shared-face-fields-20261004。一次执行包装脚本在最终打印时缺少json import导致NameError；实际跨构建和worker检查已退出0并保存结果，随后只读取文件确认，没有因包装错误重复运行。

## 判定范围

全数组新旧一致只证明writer metadata修复保持既有数值，不证明PRES/TEMP/ENTR/VORT/DIVV科学定义已独立验证。检查t=0不能替代长期演化、Restart/CUDA或冻结物理场景的性能出口。

production point仍扫描叶单元，没有引入空间索引或缓存；没有本轮native窗口UAT、原始输出上传、push/tag。未改Studio/Core生产实现，因此不重复320项既有baseline；仅新工具scoped验证及diff check。完整来源身份、独立科学review与整个联合目标仍未完成。
