# 获准 CPU 子组后的 CUDA scoped 验证（2026-10-05）

## 身份与授权

唯一工作区 ARCH-compute-optim，分支 studio/compute-optim-integration。
开始 HEAD：9c1717283f544f3e933993fc68fff793a39d3521；fetch 后 compute/optim=8fc0dd25、codex/o8-boundaries=11a321d5，无新变化、未合并 main。
已重读科学清单第6–8节。当前 CPU ELF SHA256 为7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510，本节点未改变。
JENS uniform-lifecycle-1 的9短演化+9实际restart沿用匹配该ELF的既有收据，未重复运行。
既有非均匀Jeans、Box、Radial CPU证据分别见JeansExistingFullCpuGates、BoxRadialExistingCpuGates报告。

## 真实阻断与最小修复

CUDA Release sm_89新配置位于同一工作区build-cuda；没有创建第二源码工作区或安装新依赖。
复用原本机HighFive源、系统KLU/HDF5和CUDA12.8；cuDSS最初默认搜索未发现，
后来查明现有版本在/usr/include/libcudss/12及/usr/lib/x86_64-linux-gnu/libcudss/12，并显式绑定。

实际curvilinear测试链接失败：mold遇到native对象与未提取的LTO archive，GCC lto-wrapper报no input files。
移除-flto仍失败；相同原对象/优化/LTO用GNU默认bfd成功。这是toolchain链接兼容问题，不是数值gate失败。
原SelectIpoLinker只探测全部对象进入LTO的形状；新增native-only executable+unused C/C++ LTO archives探测。
不兼容optional linker被拒绝，保留LTO并选择通过同样探测的默认linker，不全局禁用IPO或改变FP64规则。
新增确定性失败反例；8项真实CMake/GCC tooling回归通过。
最初OBJECT探测夹具违反canonical-owner审计，改为直接native source；原审计规则未放宽，最终审计与diff check通过。

## 实际运行

5个现有目标均在RTX4070Ti实机exit0，未用skip冒充PASS：

- arch_cuda_curvilinear_geometry_smoke
- arch_cuda_regrid_migration
- arch_cuda_compensated_sum：11 cases
- arch_cuda_reduction_contract：36 device edge cases，使用实际gravity execution owner
- arch_cuda_refinement_indicators

外层parallel28，原heavy CUDA pool1；memory guard未终止，swap增长0，峰值owned RSS1560148KiB。
数字是编译资源观察，不是性能结论。ELF/log指纹及既有收敛标量见summary.json；原始日志和ELF留本机。

## 覆盖边界与下一步

本节点仅关闭本次CUDA链接阻断并交付共享几何/AMR/归约/现有指标scoped证据。
没有构建完整ARCH CUDA应用，未开放JENS CUDA Runtime，未运行新CUDA JENS冻结生命周期。
现有curvilinear测试包含Host比较路径和Host/device调用警告；通过不代表每个分支都在device上执行，
也不关闭新RZ角动量语义、RZ-AXIS-01或RZ-VISC-01。
无正式计时、无长跑、无RZ新公开能力声明。依赖完整闭包仍未知。
下一节点：补齐JENS共享叶函数及实际CUDA生命周期接线/验收，再统一生产CUDA应用验证；
RZ继续按A→B→C→D贯通，待审连续Phi/force、轴线及粘性语义单列。
