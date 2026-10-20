# 最新 Host 原生 Build 与工具链稳定性取证

2026-10-03，源码 f1875686ebb49cdcb317fc87f7dcdb3792015b08，执行前工作树 clean。通过原生 Linux Studio Build 按钮一次启动 Host-owned CPU profile；此前预览已完成、无活动Run。没有单独configure、clean或更改编译缓存。

Build f6449e3d-ccc0-4d51-ac96-eaf767de62a8，2026-10-03T06:07:32.777Z 至 2026-10-03T06:07:33.689Z，exit0。原生有界terminal drawer显示 Re-checking globbed directories、ninja: no work to do、succeeded。这是无工作增量构建，不是干净编译证明。

tracked固定3项及binary SHA/size/mtime均与Build前一致。实际compiler依赖735项、object66个；三种前后稳定性字段均true。新Manifest确实包含GNU C/CXX driver、各自组件/specs的前后身份，已补入真实Build证据；不是仅单元测试或历史Manifest推断。

binaryState仍freshness-unknown，原因 Linker input evidence is incomplete or unavailable.。link depfile中的missing临时输入保持unavailable，未按文件名忽略；dependenciesComplete=false不变。前后相等不证明ABA不可变、全参数覆盖或所有implicit libraries；不能标为全部当前源码已认证。

Build后页面将旧Preview明确标为previous/stale并保留曲线，没有自动Preview或运行simulation。当前Core binary指纹仍e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7。未改配置、不Save、不生成科学输出。

完整Manifest/status留在studio/.local/integration/current-host-toolchain-build；提交仅精简工具链指纹/计数/摘要和清单。没有源码修改、未重复234项检查、没有CUDA/push/tag/main merge。原生Stop和活动任务关闭矩阵仍待完成。
