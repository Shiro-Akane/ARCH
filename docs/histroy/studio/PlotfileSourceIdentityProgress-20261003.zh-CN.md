# Plotfile case / EOS 来源身份增量

日期2026-10-03；基线ae3af9b741e3e2eb6106945a8b87d4c7221b92e9。
授权：联合计划35c5b7b114069621901386bfc4bc2a656e65af06 §4.3。
候选部分身份，非完整 provenance 或科学认证。

## 来源链路

实际 DriverIO 持有已 resolved/loaded 的 CheckpointProvenance；
write_plot 将同一只读对象交给 PlotIO，复用 EOS policy、已加载表内容摘要、
IdealGas gamma 与 ordered species names，不重新加载 EOS 或重新 hash table。
case ID 取自 SimConfig.LoadedInput() 的不可变 ConfigurationInput.case_id，
仅为实际 load boundary；不根据 config 文件名、base_name 或选中项目推定模型。
不新增 RequireLoadedValues 检查，不修改 Setup/Init、运行推进或 checkpoint。

共享 writer 可选 PlotSourceIdentity，旧调用默认无身份。
SourceIdentity/version=candidate-identity-1、scope=partial。
case_id/case_source、eos_type/eos_source、eos_table_sha256、
ideal_gamma_available及可用gamma、species_count/ordered species_names记录已知证据。
eos_table_state 区分 ideal 的 not-applicable、recorded与unknown；
species_identity_state区分recorded与unknown，不能把未知组分冒称已知零组分。
run/raw_config/effective_config/build/binary/source_git/EOS unit system明确unknown。
root plot_identity_state仍unknown，不因新增两项来源冒称 complete/verified。

身份字段有界；拒绝非法表SHA、非法IdealGas gamma/表关系、空/过长组分名。
部分数据只是 writer 调用方给出的来源证据，不是数字签名、全运行身份或
单位校准证明。reader/client尚未消费SourceIdentity，正式scientificIdentity仍unknown。

## 验证

真实 DriverIO与PlotIO object编译通过，两个CPU IO scoped target编译通过；
plotfile_publication/checkpoint_compatibility CTest2/2 PASS，diff check PASS。
manufactured1D/2D HDF5中case/EOS/gamma/species存储与输入一致，
bad table digest发布前拒绝，部分来源不能自动提升root身份状态。
独立h5py回读SourceIdentity、gamma/shape/缺失字段unknown并确认字节未变。
摘要同名Summary.json；raw H5/log留本机ignored路径。
第一次误用ARCH下DriverIO object target，Ninja正确拒绝后找到
arch_solver_dispatch target并编译成功，未绕过检查。
最后增eos_table_state/species_identity_state后重编译/CTest通过；
这两项状态尚无独立fault/语义验收，后续reader对照覆盖。
不重复未变更Studio279项及静态build；Studio/Host本轮没有改动。

## 剩余

原始配置文本目前未传到本层IO；不能重读路径当前内容冒充加载时的原文。
运行 executable/source/build身份也未传入。接线需在加载/启动边界捕获，
保持effective配置与raw配置区分，不能由当前Git HEAD或文件名伪造。
字段单位/定义来源、真实Sod/Cartesian2D AMR输出对照、发布fault injection/
科学owner review、Viewer/LOD与实际I/O/RSS仍未完成。
本轮未链接/替换production ARCH、运行simulation/CUDA、push/tag/main merge。
