# 注册模型 RZ 初始化接线与模型语义 finding

基线 2e48646561f15cdb6398f62d74a4e02ed21357a8。

## 已验证的注册链
Gaussian 保持生产模型源码不变，实际 registry factory -> RuntimeParams::LoadText(InitialState)
-> SetupChecked -> TypedProblemGenerator::InitializeData -> shared PopulateState/EOS。
内部 RZ profile/native root真实传入，256 interior cells中 passive Gaussian field 按(r,0,z)展开；
max fraction mapping error=0，背景组分/rho/momenta/ideal energy一致。
输入显式zc=.25，验证轴向依赖；没有使用旧polar z=0。
注册source SHA匹配实际Gaussian源码fb21321b48c8455973bd971f6bf7d20b7f5dfbc410bb897dff8c65c5e8f55ca9。
这证明strict配置、材料来源与共享初始化接线，不是独立科学oracle、冻结endpoint或演化验收。
用例是新增小型工程输入，不替代Core批准的科学benchmark。

首次probe以include方式带入模型但没有实际 ARCH_CASE_SOURCE_SHA256，严格来源校验拒绝。
修正为case实际CMake compile command，并验证SHA与当前源码匹配；重编模型，替换旧model object。
没有手填config identity、绕过SetupChecked、复制生产Init到前端或放宽校验。

## 待Core确认 finding：GravityBox isolated二维
simulation/GravityBox/GravityBox.cpp Init：
coords={p.x,p.y,p.z}; for a<dimension_ 累计Gaussian半径。
当dimension_=2、RZ坐标=(r,0,z)，遍历只消费r与0，忽略z。
Setup center_y仍是第二参数；当前文档未冻结RZ下这些center参数的物理映射。
因此RZ isolated二维可能退化成沿z均匀源；不能以GenericProblem正确传递context证明其模型定义已迁移。
已向Core请求center_x/y/z权威含义和有限环体density profile；本轮不自行修改该模型。
此finding与finite-ring gravity kernel/near-source/预算确认分别保留。

## 复现/身份
validation/amr/run_registered_rz_initialization.py --build <trusted CPU build>
--output-root <new ignored path>。
实际case command SHA不匹配当前文件即停止。
输入 validation/amr/inputs/gaussian_rz_init_probe.par；非正式simulation用途。
Source/header/input/ELF fingerprints见Summary。
原始失败/成功logs/ELF留studio/.local/integration/registered-rz-init-20261004及-repair。
未生成H5、未timestep、未修改模型物理；公共RZ/AMR角动量/科学验收/CUDA未完成。
无push/tag/main merge。
