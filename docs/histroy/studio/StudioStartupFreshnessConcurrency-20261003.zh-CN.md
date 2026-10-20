# Linux Desktop 启动 registry / freshness 并发修复

基线 a350df35b9ee90acc4e29a7b1a7395a6224d35b6；仅Studio/Host改动。
原生UAT中出现Sod注册列表在轮询后消失，以及旧binary的tracked inputs被显示成validated。
不是科学Core故障；没有修改Core、独立configure、ARCH Build或simulation。

## 已复现原因及最小修复

1. ConfigurationAdapter 的schema/inspection和static registry共享busy gate。
   真实重叠schema+两个discovery请求回归在修改前报409；
   增加独立registry in-flight合并，最多一个registry child及一个configuration child。
   静态注册仍是selected-binary身份，无fieldModels/AMR执行profile；
   registry成功不开放Preview或初始化。
2. BuildRunner.refreshFreshness扫描前清空共享changedInputs，异步扫描中间态可见；
   新测试确认旧实现扫描期间从['case.cpp']变成[]。
   改为局部证据，结束且manifest仍相同才发布；重叠请求共享扫描，
   Build finalization等待前一扫描后再测新manifest。
   Preview readiness消费该完成扫描的返回snapshot，不读取另一个扫描的中间态。
3. InitializationWorkflow收到Host readiness后同时核对非selected-only scope，
   才显示tracked inputs match并开放初始化；
   selected-only显示current tracked-input validation unavailable。

两个新增回归均先证明旧实现失败，再验证修复；不删除或放宽测试。

## 原生UAT与诚实记录

第一次重开窗口只带registry/文案修复，旧freshness逻辑仍发生false-ready，
并由既有自动机制产生过一次Sod Initial Preview；
不是simulation或新Plotfile，也不能算作成功freshness证据。
该窗口PID827028已正常关闭并确认/proc消失。
随后带完整修复重开PID839715，production assets index-C8OO8Leo.js：
Sod registry与compiled source关联稳定，经过多轮轮询仍needs-build，
Preview/初始化/AMR禁用，没有生成新预览。
旧失败与本次通过分别保留，绝不以单元测试替代原生UAT。

311/311 Studio/Host全回归、lint/typecheck/build通过；existing big chunk提示未借机重构。
具体exit code及耗时见Summary；完整日志留studio/.local/integration/registry-startup-20261003。
未重跑科学Core测试，因为Core无修改；之前t=0科学finding和单位review保持待办。
本项仅关闭启动并发/误导文案finding，不声称full dependency coverage、完整Viewer或联合目标完成。
底部Build: ready表示Build控制器可用，不代表binary freshness，二者仍由具体状态说明区分。
无push/tag/main merge/Windows适配；原始科学数据未提交。
