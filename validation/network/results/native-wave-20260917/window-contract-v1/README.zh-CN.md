# 有界窗口／因子 cohort：18 项真实 GPU 合同通过

2026-09-17，leaf-inline 完整结束并双端保全后单独运行。两个 Host 对象新编译，
链接已合格且不变的 native batch-kernel provider，不重编 CUDA factory。
本轮是制造解/调度合同，**不是核反应、完整 Helm、AMR、安全或性能验收**。

151／201 阶分别测试 `(window, cohort)`：
`(1,1), (2,1), (3,2), (8,8), (9,8), (32,32), (33,32), (64,32), (128,32)`，18／18 全过。
原独立已知解 `1e-12`、CSR residual、输入不修改和严格负向错误分类不变。
覆盖过期/移址/零 token、错误操作/指针/跨页别名、空页/尾部、混合请求、逐出恢复、显式失效，
以及末页 NaN 失败后不发布前面页新身份、合法旧状态恢复；基础设施错误不算负例成功。

## 成本不能隐藏

同一维度 151 或 201 的物理调用计数在以下制造序列中相同：

| window/cohort | native pages | 因逐出恢复 | 因失效恢复 | factor calls/systems | solve calls/systems |
|---|---:|---:|---:|---:|---:|
| 32/32 | 10 | 0 | 64 | 6/192 | 9/288 |
| 33/32 | 18 | 9 | 64 | 17/544 | 17/544 |
| 64/32 | 18 | 267 | 64 | 17/544 | 17/544 |
| 128/32 | 34 | 618 | 64 | 33/1056 | 33/1056 |

这些是整个正/负合同序列的累计计数，不是单步核反应成本。分页会增加 factor 恢复和 padding，
不能只凭逻辑窗口变大推断加速。32 槽下两种制造矩阵的 provider peak estimates 为
38,972,552／51,902,552 bytes，原 256 MiB 预算不变；不能外推真实网络因子占用。

Host wrapper 编译 1.14 s／RSS 139,744 KiB；测试编译 1.27 s／168,236 KiB，均零 swaps。
guard 共 17.283 s，owned RSS 采样峰 315,960 KiB，Host available 最低 114,185,068 KiB；
swap 9,256 KiB 不增长。整卡显存峰 571 MiB、free 最低 19,193 MiB，观测完整，未触发资源护栏。

下一步才是新 CUDA factory 接入同一 ODE continuation；须保留原网络和小输入，
不能复用旧对象，也不能把放宽单块 pool 当作跨 block 聚合。

## 证据与备份

- [22 条实际命令、18 项原始计数与产物身份](records/ARCH-native-wave-v4-20260916/window-contract-v1/record.json)
- [收集回执](window-collection-v1.json)、[完整本机字节核验](window-local-receipt-v1.json)
- raw：1,003,438 bytes，SHA `56e87f7564043f825264fa6bb8e148eec8af64bbd24eb45e3e564facf19c6b42`
- compact：89,258 bytes，SHA `1c653097603ad638a5232d03d6298a6390fea9777670cf864b56eb627e3149bd`

93 个 raw 文件共 2,225,689 bytes；89 个投影文件和 4 个 raw-only 产物全部核验。
原 provider SHA `0b902a1e7d87174bc395d4be328713390d36a28320353d55113678e57f697204` 不变。
服务器和本机保留完整 raw；Git 保留冻结七文件输入、源码、命令/日志、清单和回执。
