# Plotfile EOS constituent 只读查询与来源面板

## 联合计划位置

3C 后独立 plt 小切片，源码基线 568026113e09ebeb03eb0b92a55fa35096660d4e。
上一交付已记录 runtime CheckpointProvenance 的 A/Z/gamma/Cv，本轮接入 Host/client/Viewer。
没有修改科学 Core、writer、原始 H5/checkpoint 或 Build/Run/Preview 配置。

## 契约

PlotfileSourceEvidence 新增 optional speciesProperties。
旧文件缺项时不构造参数；显示 not recorded in this file。
version=checkpoint-species-1；
recorded 必须来源 resolved-runtime-checkpoint-provenance，
values A/Z/gamma/Cv 四个数组严格与 speciesNames 同长度/顺序，最多128组分。
Host 在读数组前要求 dataset FP64 [Ns]，随后要求值有限；
未知版本、float32、shape错误、NaN、orphan数据/缺版本以及 unknown却有值均拒绝。
unknown 状态保留 source=null/values=null 和非空 reason，不从 schema/UI 回填。
Client 也验证上述值和来源，不仅信任 Host 返回对象。

来源面板采用可展开表格，按文件组分顺序展示原始值。
Cv 标 raw，不猜单位或换算；不生成新的复合 EOS fingerprint。
整体 scope=partial、completion=unknown、renderEligible=false 保持。

## 自动验证

7项 source evidence scoped tests 通过；
完整315项 Studio/Host test、lint、typecheck、production build 全部通过。
production bundle index-DvnQdgEl.js；摘要记录 SHA 与耗时。
Node build 保留既有 large-chunk warning，没有为消警报重构。

真实 Sod Ns=1 / CellularDet Ns=19 文件经 Host/client metadata/slice/LOD/point 通过；
Host 返回的四组 property values 经独立 h5py 逐位回查，差异0。
production isolated worker 也完成读取、BUSY、启动期cancel/reap/recovery，
仍受原64KiB/15s/文件预算限制，没有提升预算或引入cache。

## Linux 原生 UAT

使用已授权 Computer Use，真实 Linux 独立窗口，经窗口 Ctrl+R 重载 production assets。
最初截图捕获了其他前台应用，未在错误画面操作；
激活唯一返回的 ARCH Studio 窗口后正常捕获并完成验收。
进入 Real Plotfile，项目相对路径读取原有真实文件，不用browser automation或DOM。

Cellular 显示19组分，展开后 h1→neut/prot 与 source说明可访问，
Cv=0 保持记录值，不误判为缺项或回填其他值。
Sod 显示 SodGas A=1/Z=1/gamma=1.4/Cv=1，table与source可读。
unknown run/effective config/build/source Git 和 partial说明保持显示。
旧文件缺项/unknown语义由自动HDF回归覆盖，本轮不声称额外 native legacy验收。

轮子注入未发生位移，滚动条拖动可达表尾；
先记录为 Computer Use/WSLg 操作现象，不据此宣布应用scroll失败。
当前桌面仍选择旧 Core binary/needs-build，Plotfile独立读取并未提升其freshness。
本轮未点击 Save、Build、Preview、Init/AMR、Run/Restart，不产生新的科学输出。

## 交付与剩余

更新 validation/io/verify_plotfile_reader.mjs，使摘要记录 bounded EOS constituents。
本机原始文件保持不变；提交源码/工具/处理后摘要，不提交dist/ELF/H5/完整场数组。
本轮首次修改工具时 pattern guard 未匹配，未写入；核对实际位置后完成。
checks通过后没有生产新改动，不重复完整套件。

完整 build/EOS/run来源认证、独立EOS科学参考、大文件/index/cache与后续物理/平台验收仍待。
原 scope 不缩减为 provenance UI；全联合目标未封箱。
