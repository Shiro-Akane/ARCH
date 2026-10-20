# Linux 原生显式覆盖保存验收

2026-10-03。基线 a946d9826628a5b1decab76a28a0db79a9d7ce7e，开始前 tracked working tree clean。
使用 Linux 正式入口和独立 production Electron 窗口（724458），Host PID 87403。
本轮只验收本机文件生命周期；未运行 simulation、真实 Preview、AMR、ARCH Build 或 CUDA。

## 测试输入与操作证据

从已经使用过的独立 Sod 输入复制测试副本，刻意命名 CellularDet.par，保持 selected case 为 Sod，
触发已有 filename/model suspicion 确认。该副本不代表 CellularDet 科学输入。
原文件及字节备份保留；完整输入、预期内容及日志仅在 ignored local evidence 中。

1. 原生搜索并编辑 cfl，0.4 → 0.41。Inspector 显示 Working Copy 0.41、Saved value 0.4。
   startup/edit/refresh 出现 Preview pairing confirmation，全部取消，没有 Continue Preview。
2. 点击 Save，滚动查看覆盖确认：Current Model Sod、准确目标路径及
   “Preview confirmation does not authorize overwriting this file.”。
3. Cancel overwrite 后，磁盘文件与原备份逐字节相同；Dirty 与 Working Copy 0.41 保留。
4. 再次 Save，显式点击目标 Overwrite。UI 显示 Configuration saved to disk、Saved、Disk in-sync。
   文件逐字节等于预先生成的唯一 cfl 修改预期，未插入缺失 diff_cfl。
5. 修复布局后刷新 production assets，保留已经保存的 0.41；再次打开确认观察所有按钮，
   并执行相同内容覆盖验证按钮可用。原生 Reload disk version 后 Inspector 的 Working Copy／Saved
   均为 0.41。此第二次覆盖验证按钮可用，不扩称第二个独立 Dirty-save 验收。

## 发现和最小修复

原确认面板的 CSS grid 使用默认 min-content 列宽；按钮继承全局固定 height:32px。
长相对路径使文字越界且覆盖按钮多行文字与邻近按钮重叠。
只修改 studio/src/styles.css：
确认 grid 使用 minmax(0,1fr)、长路径允许断行；确认按钮 min-width:0、auto height、
32px 最小高度与明确 padding。保持当前完整目标文字和所有确认条件、磁盘 fingerprint 检查。
修复后原生 production 截图观察到目标文字在边框内、按钮互不重叠，滚动后全部可访问。
确认仍位于 Save 上方，需要用户滚动查看；本轮不声称实现自动聚焦。

## 文件与构建身份

- 原输入／备份 SHA-256：9edb45ef29ce5bbf371f9145f5bc1dbb051ee31563472aaee865e159e59b3d16。
- 保存后 SHA-256：c0a61d4f5435881012486c339071464822ce640ace41ead24f731626707bdeec。
- 原始 source 与备份未变，只有独立验收副本修改。
- CPU binary SHA-256：e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7，未变。
- Build identity 保持既有 f6449e3d；完整依赖 freshness unknown，不由本轮覆盖验收升级为 current。

## 检查、覆盖范围和保留安排

修复后 Studio/Host npm test 234/234 PASS，0 fail/skip；
lint、typecheck、production build、git diff --check 均 PASS。
既有 bundle size warning 保留；无新增镜像 CSS 的单元测试，真实原生 UI 验证布局。
本轮覆盖确认／取消保留／真实落盘／重读通过；外部文件冲突拒绝证据沿用其独立记录，
不把本轮确认当作外部冲突绕过。
仅提交样式、精简报告和 JSON；raw input／expected／before-after／完整 regression.log 留在
studio/.local/integration/native-overwrite-uat-9bc3a517-d692-43c9-91c0-7d1b362c157f。
未 push、tag 或 merge main；3C 仍须按联合计划形成逐项出口审计，不宣告完整科学计划完成。
