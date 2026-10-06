# RZ 原生 AMR root / leaf 长度域接线

基线 72b8679176aacc68ea533013a6d316657dbfcc29。
Block::InitGeometry、AmrTree::InitRootGrid、LoadLeafGrid 接收同一 explicit GeometrySemantics。
explicit profile 在修改 root/active storage 前完成 domain preflight；
root 与每个 native leaf 的 Grid.InitializeTopology 传入同一 profile，RZ z不被当作角度。
默认 Existing 保持历史 per-block polar 校验，不对旧输入自动迁移。
read_chk 在 verified geometry identity 后，将RZ profile传入实际 LoadLeafGrid。
没有建立第二个 AMR tree/transfer owner；没有改动 refine/coarsen 数学。

## 证据
实际 native RZ checkpoint fixture 改为5leaf mixed层级：[1,1,1,1,0]，
两个径向root中一个细分，z=[-10,10]；细层z宽10、粗层20均超过2pi。
write_chk -> HDF -> read_chk全部native bounds/原始FP64/组分/controller恢复一致。
curvilinear_metrics / amr_operation_plans / checkpoint_compatibility 3/3 PASS；
mixed checkpoint fixture最终追加后仅相关checkpoint test复验PASS。
共享 PopulateState/EOS fixture RZ z=[-4,4]，actual root creation使用RZ profile；
full-ring measure=32pi、z-dependent字段和repair体积/质量/能量独立积分一致；
最大energy算术差7.10543e-15，现有工程预算未变。
unknown root profile被拒绝，已有leaf IDs/pool count/rho不变；callback前保护继续通过。
processed source/header/ELF身份见 Summary；raw logs/ELF/checkpoint留本机ignored/build测试目录。

## 范围与未完成
恢复现成mixed leaf topology不等于执行RZ refine/coarsen或证明角动量守恒。
Native root/profile参数仍由内部caller显式提供；公共configuration/dispatch仍Existing。
实际Driver RZ regrid仍由既有Stop Gate拒绝，AMR角动量传递等待Core选择权威约定。
有限环重力、公共API/IO整体切换、evolution/restart科学验收及CUDA还未完成。
本轮无simulation/timestep、无push/tag/main merge，不上传原始数据。
