# 新CPU executable的t=0 IO数值对照

编译源码869ae3d3；measurement基线b168728c；binary f82bb7ff16c4acf54ae84970b9b403dce3d0370a468d241169519b3bfd1f6a44。
因shared初始化/Grid/AMR/checkpoint源码变化，对已有明确CPU tmax=0参考输入执行一次/case回归；
不重复无变化baseline。只改out_dir，plt_variables=ALL及全部科学输入保持参考原值。
Sod 9 + Cartesian CellularDet 28 =37个完整FP64字段逐位一致；
各自checkpoint20个numeric dataset逐位一致，新checkpoint geometry revision1/chart existing；
time=0/step=0，未simulation timestep。新增独立run UUID身份不用于字段相等判断。
checkpoint numeric对照包括species属性；原始dtype/shape保持。此处不外推Plotfile EOS属性的独立科学认证。
真production isolated native-point reader/client读回run/raw config/binary身份，
文件SHA前后不变，不用LOD值代替native值。
初次Node命令使用未安装tsx而失败；按项目package脚本采用Node24原生TS执行成功，未安装新依赖。
完整输入/文件/binary SHA和scalar结果见Summary；raw H5/checkpoint/日志保留
studio/.local/integration/current-cpu-plotfile-t0-20261004。
既有verify_plotfile_run_identity_t0新增--attempts=1/2（默认2保持兼容），避免cross-build回归重复UUID测试；
显式确认t=0和新增checkpoint chart。未改变writer或科学场值。

这是原值/身份/读取接线回归，不是独立PRES/TEMP/ENTR/VORT/DIVV科学正确性证明，
不代表演化、evolved restart、完整RZ、CUDA或冻结benchmark通过。没有新增desktop UAT。
没有push/tag/main merge；不上传原始科学数据。
