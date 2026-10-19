# Plotfile 部分来源证据 Reader / UI

日期2026-10-03；基线82ab50fa580ae726724b0400b713276e28dd1db3。
状态：候选SourceIdentity贯通reader/client/Inspector，仍不是完整科学provenance或Viewer。

## Contract

reader按本地SourceIdentity group的candidate-identity-1读取case、精确raw-config SHA、
actual main executable SHA、来源、EOS policy/table状态/gamma、ordered species。
没有该组的legacy返回candidateSourceIdentity=null，不由当前项目推断。
string/array长度有界，组分最多128，数组用bounded slice读取，不加载场值完整数组。
候选全局root身份只能unknown；scope只能partial；
run/effective-config/build/source-Git/unit-system只能unknown，任何未经支持的claim拒绝。
eos table区分recorded/unknown/not-applicable；IdealGas必需合法gamma且无table digest。
校验同时供Host reader和客户端使用，拒绝source/摘要/状态关系不一致。

audit-1/audit-slice-1增加candidateSourceIdentity可选字段，既有核心字段不改。
scientificIdentity仍unknown、renderEligible=false，不因partial已记录就认证科学结果。
主executable不代表共享库或构建依赖，raw配置不代表effective配置。
metadata面板和每次samples自己的Inspector都展示各自文件的recorded evidence；
读取失败保留旧samples时，不能混用新文件来源身份。
React文本输出，文件digest用于样本关联，记录自身不构成数字签名或freshness证明。

## 验证

新3项tests覆盖partial语义、错误摘要/source、伪build/unit/full范围、
IdealGas/table冲突、空/超量组分、reader/client一致、legacy未知。
npm全回归282/282 PASS，lint/typecheck/build/diff PASS。
Host检查已包含全npm测试，未额外重复相同suite。既有大bundle警告保留。
真实C++writer manufactured1D/2D H5由Node读取，验证记录的case/EOS/原文与binary SHA，
client接受后React Inspector SSR输出来源、摘要、partial未知提示，legacy提示通过。
SSR不是原生桌面布局/点击UAT，也不是真实Sod/Cellular AMR科学验收。
raw H5/HTML/日志留本机ignored；仅处理摘要提交。

## 下一步

字段单位/科学定义来源、真实Sod与Cartesian2D AMR输出逐值检查、
publication failure/interrupt tests及owner review待完成；
全域/局部viewer、zoom/pan/轮廓、Displayed LOD/native查询、I/O/RSS测量未完成。
首次全域可能扫描大量叶块；现有audit每次全文件hash与64MiB限制仍如实保留，
不能以小响应冒称生产大文件支持或低读取量。
运行构建证据与effective配置缺口未伪造补齐。
本轮无Core build/production ARCH替换/simulation/CUDA/push/tag/main merge。
