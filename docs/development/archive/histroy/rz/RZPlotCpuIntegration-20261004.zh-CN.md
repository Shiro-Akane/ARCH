# 内部 RZ IO 修改的主 CPU 集成

源码clean b38f44a8d51a97e19a0d133a5ab547390d01f53d，
既有CPU Release ARCH -j8构建12步、21.010s。
主ELF ca6437d283c23c8f94aa562b5678273b2c06e1c5df3cc79df38ebe40c7e2e069，
7408696 bytes；本机ignored目录保存build log与显式592项input SHA清单。
保守tracked input摘要086e433617459558fcaca0c2e8a9977e84d65e01980c1ea3ed2b36cb1bed815e，
不冒称独立精确compiler/shared-library dependency closure。

新主binary使用既有批准tmax=0输入，各一例：
Sod9/CellularDet28个完整FP64字段与既有参考逐位一致；
各checkpoint20个数值dataset原字节一致，time0/step0与existing chart保持。
raw config、真实binary与OS output-session UUID身份匹配。
只是Cartesian默认路径数值回归，不是独立EOS/diagnostic科学精度验收。

使用新主solver archive重编实际Driver publication fixture：
write/flush/close/rename/create异常传播、失败成功序号不变、
同编号恢复全部PASS。fixture ELF82a9a892894584a8aadc83a20e17c5c38d1cbfcb429fde19b19a9d73d00f4faa。
上一轮已有shared writer CTest通过，没有新增改动时不重复运行。
未重复无关配置/Studio baseline。

重新fetch后origin/compute/optim仍8fc0dd25eefd2243e8c36f85440bac46994e2e73，
origin/codex/o8-boundaries仍23ff77c4f08419de2b3c5eadee214da2af25784e。
没有merge/rebase/reset或push/tag；当前源码保持不变。

上一轮RZDriverPlot报告“主ARCH尚未重编”为当时历史状态，
本轮已编入主CPU。实际internal RZ fixture/readback与Reader拒绝证据仍见该报告，
不能把Cartesian生产回归升级为RZ公开能力或演化通过。
角动量transfer、环体gravity/科学预算、RZ Viewer/CUDA和O9仍未完成。

原始H5/plt/checkpoint/ELF/log留本机ignored；
这里只提交处理后构建/输入/运行身份与故障摘要。
复现工具：verify_plotfile_run_identity_t0.py --attempts 1，
run_driver_plot_publication.py。git diff --check通过。
