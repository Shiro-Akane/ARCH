# Plotfile Reader 与同机 CPU Run：小样本工程测量

## 输入与身份

测量工具 clean commit：333fd4aebfafe24a7ee9d04f369c532551bb7059。
主 CPU binary：e75300fc607f4f6078240e2a524c5b0d6085fd0d48b811369c6eaff0729ee8fc，
实际 build source：9fb34629981f434b8fc5d531adf44b3330c083c8；OMP_NUM_THREADS=1。

复用已批准 Sod 固定网格 tmax=.2，最终 time=.2/step280；
原始 input SHA 381ceb60eb6cdbf7c99b43577ff0d94731144d655514c7a8194d09ce8d88644e。
每次只改 out_dir，其它 token 全部一致，不改变科学参数/门槛或 O9 终点。
并发读既有 Cartesian CellularDet t=0 5120-cell 文件，SHA
6e94d795f07cca3d735e2e5dd6c1129b4efe6478af811fb95ffbcc2066fad755。
生产隔离 Reader 执行 DENS 32×24 overview；只读，不启动 EOS/Driver/CUDA。

## 测量与核对结果

按 baseline/concurrent 交替执行三组，未更改 CPU affinity、清 OS cache 或控制其它系统负载。

| 指标 | baseline | concurrent |
| --- | --- | --- |
| ARCH wall ms | 85.074 / 64.999 / 66.843 | 66.934 / 66.737 / 67.344 |
| median ms | 66.843 | 66.934 |

median ratio 1.001356，约 +0.136%。
这不是统计显著性或正式性能验收结论；首项启动成本、短时波动与样本数均限制解释。

每个 concurrent Run 前已观察到真实 Reader worker 存活；三个 ARCH 生命周期
均与对应查询生命周期重叠，Reader 结算后无残留，ARCH 进程亦已退出。
并未单独标记 WASM/import、HDF scan、digest 的时间边界，
不能证明所有深度 HDF 扫描都与 Run 重叠；不声称完整压力/同机长期安全已验收。
观测 5 ms HWM/rchar 可遗漏峰值与最后计数，rchar 含 Node/HDF 加载；
不能当成 dataset-only 或物理磁盘读取量。

六次最终 checkpoint 各 21 个 dataset 与既有参考一致，20 个数值逐位一致；
六次最终 Plotfile 四字段完整 FP64 数组一致。
实际 case/raw-config/binary 身份匹配，原 frozen input、参考 checkpoint、Reader 文件 SHA 未变。
每个 32×24 query 仍扫描完整 5120 叶单元，固定响应尺寸未消除扫描成本。

## 真实失败与处理

六次执行成功后，第一版 audit 错把生产 Sod_HLLC_plt_0004.h5 写成 Sod_plt_0004.h5，
FileNotFoundError。完整失败留在工具调用与本地输出记录；不是 ARCH 演化失败。
后处理修正名称，新增 --audit-existing，复用六次输出只读核对，不重复 Run。
修正脚本 hash 与测量源码/实际 binary 构建源码分别记录，不能以文档 HEAD 冒充构建身份。

本次提交脚本、处理后摘要、报告；原 H5/plt/checkpoint/log 留在
studio/.local/integration/plotfile-run-interference-20261004。
仍需大 AMR 读取期实际重叠、冷缓存/空间索引、资源隔离与批准长期轨迹验证；
没有据此宣称 O7、CUDA、O9 或整体联合目标完成。
