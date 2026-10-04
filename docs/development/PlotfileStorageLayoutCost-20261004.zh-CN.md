# Plotfile chunk／压缩布局工程对照

基线 514afb1842bfd00e00948589b20de180e9b28668；只有合成 workload 工具新增可选 storage。
Linux／WSL、Node v24.21.0；production reader、writer、scientific Core 与 checkpoint 未改。
原始 HDF5、完整日志和逐查询响应只留本机 ignored 目录：
/home/arch/projects/ARCH-compute-optim/studio/.local/integration/plotfile-storage-layout-20261004

## 方法与可重复入口

make_plotfile_query_fixture.py --output-root NEW_DIRECTORY --storage contiguous|chunked|gzip。
每个模式生成 4 和 256 个 tiled block，cell shape=[32,64]，x1-fastest；
field chunk=[1,32,64]，一维 dataset chunk=min(size,4096)，gzip level=4。
每个布局运行现有 measure_plotfile_isolation.mjs 三次；每次查询使用 fresh worker。
模式分组执行，没有随机化／交替配对，不作普遍速度排名。
每份文件 16 个 dataset；三个模式的 shape、dtype、全部原生位模式和 dataset 属性逐一相同。
本地对照脚本沿用 verify_plotfile_query_responses.mjs 四种固定请求，仅排除顶层 file 身份；
metadata、overview、point、slice 的其余完整 numeric JSON 摘要全部相同。
文件自身 hash/path/bytes 因存储编码而不同，必须继续真实报告。

## 结果

| 布局 | 单元数 | 文件 bytes | metadata 中位 ms | overview-0 中位 ms | point 中位 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| contiguous | 8192 | 739328 | 158.93 | 170.42 | 162.63 |
| contiguous | 524288 | 46159872 | 203.88 | 606.17 | 532.55 |
| chunked | 8192 | 773168 | 148.14 | 172.73 | 171.23 |
| chunked | 524288 | 46272272 | 203.01 | 617.26 | 537.13 |
| gzip | 8192 | 68452 | 149.61 | 176.25 | 173.75 |
| gzip | 524288 | 1099683 | 148.81 | 764.76 | 654.43 |

大夹具 contiguous→gzip 文件约缩小 97.6%，首次 overview 中位约增加 26.2%。
所有 overview 仍扫描 8192／524288 个单元；固定32×24返回尺寸不能限制扫描工作。
合成 linear-index 数据高度可压缩，不能外推真实场值的压缩率。
chunked 无压缩在本次样本上没有明确收益；gzip metadata 小文件 digest 更快，
但 overview/point 的解压成本抵消甚至超过收益。现在不冻结生产布局，也不取消完整身份核验。

## 检查与边界

九次真实隔离测量均 PASS，包含 BUSY、cancel/recovery、worker settlement 后无遗留及输入 SHA 不变。
这只是既有 startup cancellation 控制，不新增读取阶段/native cancellation 结论。
六文件全部16 dataset位模式一致；四类查询响应对照PASS；既有目录拒绝覆盖PASS。
只改验证夹具与报告，未重跑无变化的334项 Studio/Host、未 Build Core、未运行 simulation。
rchar/HWM保留在JSON；包括进程/import/WASM，不称纯HDF读量或硬RSS上限。
warm-cache/grouped测量，不称冷盘、真实大 mixed-level AMR 或同机演化干扰验收。
生产布局、完整来源身份、独立科学review和联合CPU/Jeans/RZ/CUDA/O9出口仍未完成。
未push/tag/merge main。
