# 配置 v3 迁移前源码审计

审计源码基线：8fc0dd25eefd2243e8c36f85440bac46994e2e73；
Studio 子树来源：c96e9da0a6d114fd0323102b73dba4a3cd8da9ba。
本文件是迁移影响记录，不是已实现的 v3 contract，3B Linux UAT 后续完成记录见 studio/STUDIO_3B_LINUX_REVALIDATION.md。

## 已确认的责任与差异

| 当前所有者 | 当前行为证据 | 联合交付计划要求 |
| --- | --- | --- |
| src/io/ConfigParser.h::Load(istream) | 按第一处等号切分；无等号行忽略；map 覆盖重复键；没有输入行位置记录 | 原始记录保留位置；坏行、空键、重复键报错；重复键报告两处位置 |
| src/core/config/RuntimeParams.h::Resolve | StandardParameters 的 fallback 填充所有标准字段；先构造 SimConfig 再检查 | 缺项与零/false 独立；只有登记允许的默认可填；条件依赖未知不能用 fallback 推断 |
| src/core/config/ConfigValidation.h::ValidateControls | 第一处失败抛 ConfigValueError；直接访问已具数值的配置成员 | 汇总所有可判定错误；不完整记录不能成为可执行配置；直接 C++ 入口同样校验 |
| src/driver/dispatch/PolicyDescriptor.h | Flux、Reconstruction、Limiter、TimeIntegrator 四表为 UseDefault | 未知方法明确拒绝；保留真实注册别名，不新增兼容逃生开关 |
| src/api/configuration/Configuration.cpp | typed default、explicit/default、成功才完整序列化；异常仅追加单个 diagnostic | v3 可空 parsedValue、缺失/无效和来源分离；部分记录仍可返回；条件三态、完整度与执行就绪分离 |
| src/main.cpp | Load 后创建 output/log，再创建模型和调用 Setup | 完整配置确认前不得创建正式演化资源；无效/未知模型不能先产生正式输出 |
| src/core/config/StandardParameters.h 与 src/data/GlobalDefs.h | gravity_G 仍登记且 G_const 可变 | 常数单一来源；旧键明确 retired；只读科学身份继续校验 |
| studio/src/host/configurationContracts.ts | version 1/2；parsedValue 和 defaultValue 不可空 | 新 contract 与 validators 同步迁移，不把 null 转为 0 或旧默认 |
| studio/src/host/configurationValidation.ts | scalar(parsedValue)；valueSource 为 explicit/default/alias | 验证 v3 nullable、来源、条件和诊断结构，保持身份校验 |
| studio/src/components/ParameterPanel/ConfigPanel.tsx | invalid 阻止 Save、Save As 和 Download | 允许明确保存未完成草稿；保存权限、冲突处理和运行就绪分别判断 |
| studio/src/components/ParameterPanel/StandardCatalog.tsx | 显示当前 parsedValue/default 来源 | Schema Default、Inspection Parsed、Preview Effective 继续分离；缺项显示待填写 |
| studio/src/components/RealInitWorkspace.tsx | copy.valid 和 build identity 控制 Preview | 新配置完整性与 Preview 适用条件由 Core 提供；不能因允许保存草稿而放开执行 |

## 实施前约束

1. Linux 3B 文件选择、Save As、Reopen 已完成真实验收，证据另记；自动检查不替代该项。
2. 首个契约交付应包含候选规范和 Core/Host 共用完整 fixtures，明确标为候选，不能伪称当前 binary 输出。
3. 按 ConfigurationContractPlan 的 19 必需、50 条件、25 允许默认、1 个 G 退役分类审查；该计数是当前审计，不是前端生产常量。
4. 不靠反复执行部分 Setup 才发现第一个缺项。case 声明/派生阶段需在完整 Setup 和物理资源构造前明确。
5. 静态 inspection 不加载 EOS、不初始化 CUDA、不访问输入表；这些未执行状态保留。
6. 显式非法且当前隐藏/不适用的值仍保留诊断，不能被 UI 隐藏消除；Remove 必须明确且可 Undo。
7. 两条 EOS 路径独立。外部加速度的必填规则按新 Core contract；不沿用旧 UI 活动轴隐藏作为完整性依据。
8. G 的历史人工缩放测试不能删键后直接重跑宣称等价。CGS 输入、物理参考和阈值由维护者确认。
9. 本轮没有改 parser、API、前端、科学逻辑或依赖锁文件，没有运行 simulation、Build 或 Preview。

## 待完成的检查

- case 声明、实际 Setup、网络组分注册与直接 C++ 构造消费者的完整清单。
- Preview / inspection / AMR / session 入口共享新完整性检查的位置。
- v3 完整 fixtures 的合法、缺失、坏值、重复、未知依赖和退役键覆盖。
- Linux 文件对话框 UAT 已完成；不再阻塞候选契约交付。
