# O7 恢复决定与已发布 checkpoint

授权来源：Core codex/o8-boundaries 11a321d5604f9ee62b9f9587c81f14de4f128bc4，已完整读取第2.1节并将其原文同步到本地执行细则。源文“checkpoint尚未推送”是Core作决定时的历史状态；随后已推送studio/compute-optim-integration，远端精确指向4a7b43075a8b41726c20af6c3ffd6fadc41dcd96。Core尚未独立复验，不称科学accepted。

待上传对象已检查：无本轮H5/plt/checkpoint/全量日志、构建对象/node_modules/.local/dist；旧sod-1d.h5为既有LFS测试资产，其pointer与origin/review/studio-v0.4.2及旧3H/3A checkpoint一致（SHA256 dadaba822af2ff977cf81a04927901485cd90b0d2502a97ef99f8e0cac50b5ed，14336bytes）。不删除或改写该既有资产。未创建tag/PR、未merge main。

初次WSL push挂起，已核对精确自有PID/命令/cwd后终止；有界非交互重试明确缺少GitHub认证。随后Windows Git仅作为传输工具复用既有非交互认证，同一WSL HEAD成功push；fetch及ls-remote确认。无token读取/索取，不属于Windows产品适配。完整push日志留本机。

恢复范围：内部粗层受控校验分离（physical Cartesian ratio<=2不变）、限定IdealGas低G密度/压力相似迁移、原Core/API/Studio实施。JENS已定公式/父态/接受宏步/容量失败按计划推进；仍须逐子例科学参考review。新RZ近远场/预算/长轨迹未确认部分单独等待，不挂起全部工程工作。

长跑仅对已通过对应短科学gate且输入/终点/参考/预算冻结的模型执行；本次恢复授权本身不是新的冻结输入包。原阈值、共享数学、checkpoint拒绝语义不变。原始H5/plt/checkpoint及全量日志留本机，只push源码/处理后摘要。
