# ARCH Studio Phase 1B Completion Report

日期：2026-09-13。基线：studio-phase1a-v0.2.0，提交 3692d5b64ec75733846e705990f6707d5444ebb7。工作分支：studio/phase1b-config。

## 结果与范围

完成真实 `.par` 读取、保留原文的 ParDocument、内存 Working Copy、Core 常驻布局、明确范围 Slider + Numeric Input、Custom 保留、软件契约验证、Revert 与 Save As。保持 Mock 和真实 1D Plotfile Viewer。按 M0→M8 顺序记录于 STATUS.md。

遵循用户最新边界：桌面/工作站是目标平台；窄窗口只做严重布局、横向溢出和控件可达性检查，不追求移动端功能完整度。未增加移动端专用 UX。

## 语法依据

读取 src/io/ConfigParser.h、src/core/RuntimeParams.h，以及 Sod、Cellular、GaussianPulse 实际配置。

- 第一处 # 开始注释，第一处 = 分隔 key/value；仅裁剪 ARCH 支持的 ASCII 空白。无 INI section、引号剥离或转义解释。
- 重复 key 大小写敏感，最后一处生效；UI 标明生效行号和重复说明，序列化只替换最后生效的 value token。
- 无 = 的非空行由 ARCH 忽略，Studio 原样保留并计数；未知 key/顺序/注释/空行/未编辑值不删除。
- GetBool 为不区分大小写的 true/false；整数/浮点依据实际 accessor；use_nse 明确支持 true/false/auto。
- 坐标支持有限数值及确认的 pi、-pi、coefficient*pi、pi*coefficient、pi/coefficient；不实现任意表达式。
- 源解析器 stoi/stod 对部分后缀有前缀转换行为。Studio 对已识别数值使用完整 token 验证，模糊后缀标为 unsupported/invalid，不假定其意图。
- BOM 原样保留，作为首个 key 的字符，不擅自替源解析器规范化。

## 验收证据

| 目标条目 | 证据 |
|---|---|
| 1 新分支 | clean Phase 1A tag 派生 studio/phase1b-config，M0 记录 |
| 2–4 真实读取与来源 | 浏览器读取 sod.par、cellular.par，显示 filename、saved 状态、Core/Custom 分组 |
| 5、10、11 原文保持 | 三个真实 fixture 无编辑往返完全一致；单参数编辑测试只改变 value；原始 fixture 与仓库来源字节一致 |
| 6、7 Dirty/Invalid | 编辑立即 dirty；范围 1.2 保留且 invalid；已知类型、有限值、枚举、条件必需项有错误提示 |
| 8 Revert | 浏览器 tmax 0.25 还原为 0.15；恢复 loaded snapshot，不写原文件 |
| 9 Save As | 实际下载 Downloads/sod_modified.par，磁盘 readback 与原文仅有 tmax 0.15→0.25 差异 |
| 12、13 Core 与滚动 | 1280×720、1920×1080 桌面截图通过；无 Core disclosure；固定左栏独立 overflow:auto |
| 14–16 Slider | refine_threshold 的 RuntimeParams [0,1] 明确证据；0.7654321 精确输入和键盘调节双向同步；非法值不 clamp；cfl/tmax 没有范围 Slider |
| 17 Custom | xhe4 当前为 0.4 仍是 text；无推断 unit/range/role，prototype-like key 回归通过 |
| 18 Mock | 浏览器重新生成 current 512×512；原有 Mock tests 保留 |
| 19 Plotfile | 真实 metadata/fields/LineVis，PRES 样本33 x=0.5078125/value=0.3054751636143017；无效文件提示及重新打开恢复 |
| 20 工具检查 | 34/34 tests、lint、typecheck、build 通过 |
| 21、22 边界 | 全部变更在 studio；无 Core、Setup/Init、Build/Run、SSH、AMR 功能或 ARCH 重编译 |
| 23–25 文档与封箱 | STATUS、完成报告更新；指定消息本地提交并创建 tag 后停止，不自动 push |

## 测试与操作细节

Parser 覆盖三个真实 .par、注释/空行/未知行/科学计数法、重复 key、CRLF/BOM、无末尾换行、空 value、单编辑最小 diff 和注入拒绝。State 覆盖 loaded/saved、dirty、invalid、Revert、第二文件及 Mock stale；读取使用已测试的 latest-request gate，旧成功/失败不覆盖新结果。类型测试覆盖 bool、明确枚举、int、float、string、有限 pi 表达式、显式范围、条件必需 restart_file，以及阈值有序关系。

Save As 请求浏览器下载，保持 working copy dirty，不谎称拥有磁盘写句柄或已原位保存。Revert 始终恢复本次 loaded snapshot。浏览器自动化的 download event 等待超时，但随后检查到实际下载文件，逐字符比对通过。

## UX 与限制

- Core 常驻，Custom 可折叠；不增加 card 嵌套、不扩大左栏、不改 Preview Renderer 风格。
- Slider step=0.001 是标注的 UI 粗调步长，不是 ARCH 参数精度契约；Numeric Input step=any 保持精确输入。derefine_threshold 的合法上界依赖 refine_threshold 且严格小于，不错误标成闭区间 [0,1] Slider。
- 最小 schema 只覆盖已有源码证据的 core keys；其余 key 以 Custom 原文保留。不宣称完整科学语义或运行有效性验证。
- 缺失 optional keys 不自动插入，遵循 ARCH defaults；已确认的条件必需项显示全局错误。编辑范围限现有 key，不新增配置 schema 编辑器。
- UTF-8 文件限 1 MiB；不支持任意编码、二进制配置、原位写入或浏览器关闭后的工作副本持久化。
- 源码接受的模糊数值前缀形式在 GUI 中可能被标为 invalid；不静默转换，不重新格式化原文。
- Preview 明确为 Mock/disconnected；真实配置编辑仅标记旧 Mock stale，不映射成真实初始条件。
- 保留已有大 bundle 警告：约 6,134.22 kB，gzip 1,398.89 kB。无新增依赖或性能优化扩展。

## 审计与 checkpoint

package.json、package-lock.json、DEPENDENCIES.md 和 DEPENDENCY-LICENSES.json 与 Phase 1A 无变化。保留导入目标文件及原始 fixture 的原文空白，暂存空白审查对这些外部原文单独记录例外，其余新代码必须通过。

commit message：feat(studio): integrate real ARCH parameter working copy。

tag：studio-phase1b-v0.3.0。最终 commit hash 以该 tag 解析结果为准，在交付消息中报告。仅本地封箱，不自动推送远端。完成后停止，下一阶段需新目标。
