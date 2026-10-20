# Plotfile 原生 Inspector 候选接入

日期：2026-10-03。基线9e284bcc8d805bd620d6079de15cde3655574035。
状态：候选原生查询接客户端/Inspector，尚非完整 Plotfile Viewer 或科学验收。

## 实现

客户端验证 candidateNativeGrid/nativeCells 的版本、共享来源、约定、单位未知、
file-local identity、logical key/level/坐标、数组长度、活动 bounds、非活动0 bounds、
有限正 measure。header/payload 一方缺失不能假装原生能力已成立。
保留 Project/file/SHA/request/index/value/coordinate 校验及512 bounded request。

Project Plotfile audit 逐行 Inspect，成功读取默认第一行，新 metadata 清空旧选择，
失败/取消保留前次数据及其原文件/field/block。新请求成功时选择归零。
Inspector 显示原始字段值、文件路径/SHA/时刻、block/global index、
无ghost i/j/k、存储Cartesian中心；候选文件另显示 file-local logical key/level、
每轴 active/inactive bounds、原始 cell measure/来源/约定。
单位与科学身份仍 unknown，候选review pending；不插值、不平均、不改原响应。
legacy 明确未记录 native bounds/measure；不从中心点补算。
这些动作只更新显示选择，不 Save、Build、Run、Preview 或改 Working Copy。

## 验证

新增客户端防伪造 tests：header配对、未知版本、global identity、错logical key、
负logical coords/NaN level、猜测单位/来源、缺长/零宽/非活动非零bounds、
null/zero/Inf measure拒绝；2D非方形/3D存储index逆向i/j/k一致。
全 npm test279/279 PASS，lint/typecheck/build/diff PASS。
Host代码本轮未改，上一增量127/127 Host regression已通过；
完整npm test也包括现有Host测试，不额外重复相同 suite。

额外 C++writer真实文件→Node reader→客户端validation→React Inspector SSR：
1D/2D各选择最后一个 bounded样本，检查 raw value/file SHA/logical identity/
shared measure source及unknown提示，文件字节未变；legacy未记录提示通过。
处理后的摘要见同名Summary.json；raw H5/HTML/日志留本机ignored目录。
SSR证明组件内容映射，不证明原生桌面点击/滚轮/布局UAT。
无 Computer Use 替代人工观察的声明，真实desktop Plotfile UAT仍pending。
既有bundle chunk warning保留，不为了打包扩大重构。

## 剩余

case/config/build/binary/EOS与字段单位来源、真实Sod/Cartesian2D AMR对照、
发布fault injection及科学owner review尚未完成。
全域显示、zoom/pan、Native AMR/Displayed LOD、AMR轮廓/点选及实际I/O/RSS仍待实现。
当前audit64MiB输入与full-file hash边界如实保留，
固定返回图像不保证首次总览读量小，不能当作production大文件支持。
未运行simulation/CUDA、替换production ARCH、push/tag/main merge。
