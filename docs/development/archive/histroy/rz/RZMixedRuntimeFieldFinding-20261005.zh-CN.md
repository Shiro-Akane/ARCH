# RZ 实际混合拓扑 field / 全局 WorkLimit（2026-10-05）

基线be3942016a6f80fbf59b5ad9d1e52ad600cc4514。
结论：真实混合拓扑field失败；RZ-MIXED-RUNTIME-WORK-01 OPEN。
不覆盖前次已通过的root field/AMR transfer证据，不声称完整接线完成。

## 真实检查

扩展原真实Runtime regrid测试，显式--field-after-regrid模式。
复用真实2→8→5冻结父态veto，五叶块1280 native cells，
r=[0,1]/z=[-.5,.5]、rho1、同一原Stage/native candidate所有者。
本轮新增验证代码，无生产Core数学修改。

计划mixed field后经合法输入粗化到2-block/512cell再重绑；
在第一mixed field ring boundary阶段已失败，后段没有执行。
Stage构造/compile六源成功，不代表field PASS。

## 失败证据与计数语义

Native RZ ring boundary failed: status=1，
target=4.7082587313629388e-18，leaf=74984、parent=25016，
两者合计100000，达到原global cap100000。
与旧512cell单leaf WorkLimit不同，这是全部boundary observers上的source tree
visit全局上限；range/kernel enclosure/AGM/adaptive boxes另有计数，不能混写。

原rtol1e-10/atol0、65536 boxes/leaf、域和rho未改。
按真实失败调用路径在RHS/Poisson前抛错，未成功发布场；
本轮没有实际执行失败后getter探针，不额外声称该反例已覆盖。
exit1；编译+测试122.228秒，owned RSS峰1302392KiB、swap0、guard未停止，
此时间不是benchmark。

完整result、测试ELF及当次producer准确指纹留本机
studio/.local/integration/rz-mixed-runtime-field-20261005和同prefix run.log。
处理后标量summary：
validation/gravity/results/rz-mixed-runtime-field-20261005/summary.json。
architecture/diff PASS不覆盖科学失败；raw数组/日志/ELF不上传。

## 下一步和不得采用的替代

不能增加work cap、放宽rtol/atol、缩小域/leaf extent，或将partial enclosure当Bounded。
原loop按每个observer遍历source tree，full-ring数学复用不充分时仍有
grid数×boundary数费用。下步在原GravityBoundary所有者审计严格相邻、
等密度、完整四子cell分区的同积分合并：
只有几何拼接/密度完全一致和原source identity/provenance能严格证明时可用；
保持原finite_ring_potential_enclosure/误差区间/完整最终账本。
不先假定整个bbox等价、不能把非均匀source替成平均密度或点质量。
这是候选后续方案，本节点尚未实施或验证。

生产RZ/Device gate保持；连续势力、mixed场成功、粗化后重绑、fatal rollback、
Hydro守恒、axis/viscosity、完整A→D科学出口和长跑仍待完成。
