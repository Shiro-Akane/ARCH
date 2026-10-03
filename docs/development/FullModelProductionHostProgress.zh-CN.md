# 全模型生产 CPU Build 与真实 HTTP Host 验证

2026-10-03。正式Host-owned Local CPU Release Build，源头为当前Linux集成工程；
没有复制build-cpu的binary、伪造Manifest或独立configure。

## Build / provenance

Build ID: b122233f-61aa-4fd5-932a-a27ded45e302
source Git HEAD: a299395c4cc312e55af704ccd7f72b836c13d7b7
Build开始时repositoryDirty=false。
旧binary SHA256: e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7
新binary SHA256: ee3de6cf2b8cd54bc54e70a5c15ffdaa60a9fb39281ae243800f21d1286c2d5a
使用原profile固定parallelism4，12项增量编译/链接成功。
实际Manifest记录source/build/target/binary/toolchain/inputs；原始Manifest与events保留本机。
binaryState=freshness-unknown：Linker input evidence is incomplete or unavailable。
不是clean-from-scratch构建，也不声称全部依赖current。
真实binary发布14模型、22个case/dimension Profiles。

## 真实 Host / HTTP

独立localhost Host复用正式openProject/createHostServer；不使用mock executable。
14个维护模型使用未保存stdin文本，经过inspect-config→按实际dimension选择Profile→
field Preview→initial AMR。全部请求终态成功，Core/Host/config SHA身份一致；
field与AMR的project/case/config/build/binary/EOS source/native coordinates匹配。
RT在64-block预算下返回limited/complete=false，保留真实状态；其余本轮mesh为ok。
不将Host succeeded等同于所有AMR complete。
初次本地验证脚本误将完整Host identity传入只接受Core identity的validator，
失败未计通过；修正脚本后完整最终矩阵通过，没有改Core或放宽validator。

## 接入中发现并修复

生产静态配置inspection有意使用selected-binary:SHA，不是成功Build ID。
UI维度选择只允许其精确SHA与当前成功Build binary一致；Preview本身仍由Host Build门槛控制。
拒绝旧文本、其他project/case/SHA和任意其他Build身份，不提升静态检查为Setup/simulation验证。

最终Studio/Host244/244、lint/typecheck/production build/diff PASS。
已有needs-build/Preview readiness不一致已在a299395c独立修正并真实复验，
本次Build后known tracked inputs无变化，但完整dependency coverage仍unknown。

## 未完成

真实Linux desktop全模型/三维/AMR交互UAT待进行；本项HTTP证据不替代桌面验收。
完整三维混合细化组合、独立plt、JENS/RZ/CUDA、冻结科学预算及性能/长时验证未完成。
本次没有simulation/Save/科学输出。原始响应数组仅留ignored本地；摘要按模型提交。
不push/tag/main merge，不标记联合目标完成。
