# 获准架构迁移及 JENS 父态静态子 gate

Authority：08ae94684d433ddbb7398a9de88c4fb9d09070b4。该提交直接继承4b5e496a；在clean branch上fast-forward接收，只同步review决定，没有main/O8 merge。

## 两条 finding 的实际关闭

生产 tools/audit_architecture.py 只应用已批准patch：
RuntimeParams同时要求AnalyzeConfigurationInput和RequireDeclaredInputs；
burn controller的精确source tuple仅增加CompositionInput.cpp普通源，三EOS OBJECT的owner/顺序/link不变。
既有架构夹具同批迁移；九类反例全部拒绝。原独有反例、缺项/非法bool保护保留，没有wildcard、backend_core借用或skip。

- 既有architecture套件108/108 PASS。
- 既有tooling discovery完整409/409 PASS，使用已有NumPy/h5py venv。
- 真实单一implementation源树tools/audit_architecture.py exit0；最后加入JENS测试后再次audit exit0。
- 实际配置/API/checkpoint执行gate 7/7 PASS。
- 因此两个本地finding关闭；不声称GitHub CI已运行，也不等于O7全部完成。

首次完整architecture测试仍扫描了两份历史fixture源码，完整错误保留本机。
先确认无进程使用，再原样移动到/home/arch/validation-archives/o7-pre-review-fixtures-20261004。
没有删除raw data、创建新checkout或修改审计范围；位置映射保存在本机和处理后摘要。
初次完整tooling使用system Python，6个科学依赖导入错误；不skip/修测试，改用现有venv后完整通过。

## JENS：父态静态参考开始落实

在现有arch_jeans_diagnostics目标扩展6个父态样本，Cartesian / 内部RZ轴线 / 离轴各单/双组分。
实际共享链为GridMetrics物理体积→restrict_family守恒restriction→父EOS声速→Jeans数学。
手工子态含不同rho、不同组分、相反速度；体积积分rho X后求父组分，
不平均声速、N_J或T，不从父能量中删掉未解析动能。

独立long-double参考不调用待测restriction/EOS/Jeans：
Cartesian体积或pi*(r_hi²-r_lo²)*dz；积分rho、动量、E、rho X；
父比内能=E/rho-|mom/rho|²/2，再用冻结caloric law和literal pi/G求N_J。
共享限制符和物理公式没有变化，没有新增target/CI矩阵，也没有打开生产JENS或RZ能力。

实际6/6 PASS，最大N_J相对误差2.7056947525674304e-16。
用于该良态静态样本的工程误差上界为gamma_12（12*u/(1-12*u)，u=epsilon/2）
加已有IdealGas/Jeans 16epsilon闭合界，合计4.8849813083506904e-15；
这来自4项正体积积分/尺度转换及state recovery的舍入传播，
不是按实测误差乘倍数，不更改任何原演化科学阈值或高Mach预算。
既有197个指数域可表示样本、104个不可表示拒绝、24个IdealGas样本仍通过。
第一次调用只执行原测试（新函数未接入main），已修正、重编译，并确认JEANS_PARENT_CASES=6后才记录覆盖。

这个checkpoint没有验证阈值等号、容量失败、初始及接受宏步、regrid/restart、关闭路径或CUDA。
这些按获准规则继续；不将静态父态通过写成生命周期完成。

## Review 出口与待审设计

处理后身份、指标和校验范围：validation/gravity/results/o7-approved-review-20261004/summary.json。
原始日志、JUnit、fixture归档留在本机，不上传H5/plt/checkpoint。
production ARCH binary未重编译；本轮新执行物仅为扩展的Jeans test，SHA在摘要中。

JENS的限定静态参考已有Core认可；真实表/Helm覆盖沿各EOS既有参考与预算补齐。
RZ保留Lz=sum(m_phi*W)，W=(2*pi/3)*(r_hi³-r_lo³)*dz。
sum(m_phi*V)不作为另一项必须同时守恒的物理全域量。
原Lz finding继续开放：需要统一state/flux/source/transfer/reflux/EOS/axis/IO/checkpoint/CPU-device设计；
不只修prolongation冒称关闭，也不直接换成r*m_phi跳过消费方。
环体源内/接触、cell/face平均、生产near/far及分项预算仍独立待审。
