# O7 JENS 原生 Plotfile 消费者

前置 checkpoint：2c066a742f41916d0c3d7e8df78622ed247e238f。
验收依据为 Core 08ae94684 的声速/父态决定及 4d9f38ac 的 bounded 契约；
本页不是 uniform-lifecycle-1 三通道科学演化的验收结果。

## 已实现

PlotIO 遍历真实 active leaf interiors，使用当前 EOS pressure 和 Gamma1 回调恢复
adiabatic cs²=Gamma1*P/rho，再调用唯一共享 JeansDiagnostics::evaluate_cell。
physical spacing 由每个 native grid 和显式 geometry semantics 提供。
不使用 configured fallback gamma、扣均值密度、JENS floor 或阈值夹紧。

Data/JENS 保持 FP64、原 writer block/单元顺序，unit=1、basis=scalar、
centering=cell、meaning=jeans_length_over_max_active_physical_spacing。
原 DENS/ENER/velocity/species 数组不因输出诊断改变。无 self gravity 的显式内部
JENS 请求拒绝；nonfinite/nonpositive EOS 或不可表示的诊断拒绝 HDF5 发布，
不覆盖最后成功文件。

本轮未解除 RuntimeParams/RefinementSelection/公开 API 的 JENS gate：
尚需配置条件、checkpoint 身份与完整演化消费者验收后统一开放。
新增单位声明是科学定义，不表示所有 model/Preview 路径已支持 JENS。

## 实测证据

真实 CPU DriverIO fixture 编译与执行通过；Cartesian 16 cells 与内部 RZ
256 cells 写出并读取真实 HDF5，publication=complete。
独立只读工具 validation/gravity/check_jeans_plot.py 使用 Decimal120、实际 FP64
单组分 gamma、输出 conserved energy/density/velocity 和 native physical bounds
计算参考，不调用 Core EOS/Jeans 函数。
最大相对差：Cartesian 2.1115519111258224e-17；
内部 RZ 3.1714519106408167e-16，均低于已批准良态静态 16 epsilon。
同文件比较原 DENS/ENER/velocity/species 数组逐值不变；
nonfinite Gamma1、gravity none 与 last-successful retention 负例通过。

完整 CPU ARCH 与原 plotfile_publication target 重建通过；
实际源码 architecture audit、diff check 通过。
最终 configuration_api_contract、jeans_diagnostics、plotfile_publication：3/3 PASS。
首次夹具编译因调用 temperature 的错误接口失败，已改为既有
get_temperature(rho, specific_internal, X) 后重新编译运行；失败日志保留本机。

原始 HDF5/ELF/日志位于 studio/.local/integration/o7-jeans-plot-corrected-20261004；
第一次编译错误位于 o7-jeans-plot-20261004，guarded build 位于
o7-jeans-plot-build-20261004。仅提交处理摘要和只读验证工具，
没有上传原始 HDF5、checkpoint、plt 或 full logs。

最终 CPU ARCH SHA256：50d58433b7065f71153a6c9843f22e21444359bed8fd61ed5807a7c792b22cc8。

## 未完成

该 fixture 来源身份为 partial，case/config/build identity 明确 unknown；
不把它冒充经过完整项目身份认证的科学运行。
静态输出不代替 accepted macrostep 演化、低密度、restart 或 CUDA gate。
内部 RZ writer 检查不解除 RZ public dispatch/regrid gate 或 Lz finding。
下一步完成 jeans_cells 条件注册、显式不适用错误、ALL 过滤、
checkpoint control identity 与 Studio 消费，再执行冻结的 GravityBox
关闭/仅输出/约束通道及实际 0.01→0.02 s 续算。
