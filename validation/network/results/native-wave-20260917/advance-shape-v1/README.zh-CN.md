# ODE 线程块布局 ABBA：数值一致，但单线程 block 更慢，不采用

2026-09-17：150/200 × BD/ROS4 × 32、1、1、32 threads 共16次真实诊断全部完成。
每次32→33单元、pool32、四步至1e-10，原rho/T/cv/rtol/组分和数值预算不变。
同一个已编译可执行文件、相同观察器，仅六个精确匹配的IdealGas ODE入口可改grid/block；
原32线程时一个block，候选每线程一个block、最多32个block。实际入口、calls和block数均确认。

## 实测

以下为每次八个CPU/GPU宏步中GPU累计诊断秒；两次同配置取均值比较。
这不是五次交替的无观察器正式性能，也不是独立反应率／sanitizer资格。

| 网络／ODE | 原32线程两次（秒） | 1线程两次（秒） | 候选耗时变化 |
|---|---:|---:|---:|
| 150／BD | 3.930765／3.952582 | 4.238910／4.219485 | +7.29% |
| 150／ROS4 | 13.246436／13.246540 | 13.929678／13.987153 | +5.37% |
| 200／BD | 5.238846／5.263063 | 5.761025／5.769064 | +9.79% |
| 200／ROS4 | 18.584493／18.622274 | 19.824413／19.843328 | +6.61% |

四组的科学metrics在两种布局及ABBA重复中一致，场／limiter最大值与原容量验证相同；
观测的advance调用数分别788、1342、788、1302，没有launch错误。
**不将1-thread布局接入生产，也不凭“更多block”声称更高性能。**
没有由这个结果排除所有可能的协作算法／布局，只否决本次已测的1线程标量布局。

## 边界与资源

重编的只有Host preload观察器；两个CUDA factory及private provider保持原身份。
没有CPU回退、精度／公式／反应网络更改，也没有新增设备fence或event。
Host计时可受观察器影响，不能当作设备kernel时间；下一步按实际成本定位，而非继续猜block数。

worker exit0，guard248.126秒，Host最低可用114,111,060KiB、采样RSS峰值387,760KiB；
swap9256KiB不增长。整设备GPU峰值14682MiB、最低余量5081MiB，I/O full最大0.056%，
护栏未触发，source/network/vendor/artifacts/recipe前后核验完成。

## 双端保全

- raw：7,715,100 bytes，SHA `9c42a26afe5013a9d1a8438e37881a17e6d8831da694af3fa8d2647ebf8999d3`。
- compact：60,733 bytes，SHA `98c800a3739865eb105f17700f2f98f90118aa47698ec97bf98458e28a5ef387`。
- 服务器前缀：`/home/ubuntu/projects/ARCH-native-wave-v4-20260916/advance-shape-`。
- 本机：`C:/tmp/ARCH-perf-20260909/build/advance-shape-v1-download/`。

71个原文件／14,026,204 bytes、68个原字节投影、raw内嵌投影及完整库存逐字节核验。
3个二进制只在raw保存。见[收集回执](advance-shape-collection-v1.json)、
[本机回执](advance-shape-local-receipt-v1.json)。保全后才启动不同目录的API成本诊断。
