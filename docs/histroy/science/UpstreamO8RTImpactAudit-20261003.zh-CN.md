# 上游 O8／RT 增量对当前联合交付的影响

2026-10-03；准确 SHA、共同祖先和交叉文件见同名 Summary.json。
本轮 fetch 只更新 remote refs，工作树未 merge/cherry-pick/rebase/reset。
compute/optim 仍为8fc0dd25；未在该分支取得新的JENS/RZ科学批准引用。

## 实际新证据与适用范围

codex/rt-amr-validation-notes-20261003（2ee82665）记录 main25adec42 的
RT reflux微量负组分失败，候选后续RKL内部失败和跨后端分歧。问题报告并非已验收修复。
codex/o8-boundaries（ee091b35）提供后续共用PPM、组分接受带和面通量修复，
RTAmrDataCorrectnessRepair记录有界修正记账、连续限定器、实际CPU/CUDA短轨迹、
restart和同时间比较的证据及纯相对接近零报警。上游报告不等于本机复验，
也不等于完整t=8.5、所有EOS、所有网络或冻结长轨迹验收。

本地RT桌面证据只覆盖Setup/Init与limited初始AMR，不执行演化；
没有将上游故障或修复结果移植到本地证据，也不凭初态成功推断RT轨迹成功。
后续第二平台RT演化不得混合旧binary和上游修复轨迹；必须先明确采用的科学基线，
复验对应共用叶、配置/身份和冻结输入，再生成本机证据。

## 集成交叉点

上游O8新增两模型，报告注册16；本地binary目前注册14。
模型数量仅是精确binary的观察，不更新前端硬编码或冒充已发现16模型。
上游checkpoint7及用户边界身份与本地配置v3/G迁移和RZ几何身份存在交叉。
两新增示例势回调仍读取G_const；本地O7.0已退役可写G，
不能整文件移植后恢复旧成员，也不能删掉公式所依赖的共享CGS常数一致性。

所有双方改变的文件已由git相同共同祖先diff求交，见Summary；
这是潜在整合位置，不是已执行cherry-pick产生的冲突清单。
配置存在性/必填、选项适用性、模型注册、checkpoint、调度和共享数值消费者
需按职责保留双方语义。RZ轴序/法向/度量将影响新边界实现，
应由共同权威演进，不复制第三套公式或仅换标签。

## 下一步与保留门槛

O8本身不在原委托实施范围内，本轮不顺手实施其功能。
若维护者确定交付基线要包含该分支，先在独立integration worktree做增量审计，
逐项核对O7构造/G退役、预览基础、边界身份及共用数值补丁；
不覆盖当前Studio/Core，不把该分支旧Sod/Cellular能力表覆盖本地全模型扩展。
取得准确整合范围与科学确认后再运行受影响CPU检查，随后按原顺序处理CUDA。
不使用上游已完成GPU检查代替本机设备执行，不放宽科学预算。

本次只读审计并保存精简引用，没有运行测试/构建/Preview/演化，
没有上传原始数据、push/tag/main merge。JENS、RZ、正式plt科学语义、
当前native zoom/pan、完整模型桌面矩阵及第二平台科学验收仍保持未完成。
