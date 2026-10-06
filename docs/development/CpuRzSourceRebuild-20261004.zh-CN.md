# 当前 CPU ARCH 完整源码重编与 API 回归

源码基线 869ae3d3d30e8a9b16e377112af73e737b6c736a；build/source一致，构建/验证前后working tree clean。
CMAKE_HOME_DIRECTORY=/home/arch/projects/ARCH-compute-optim；
Release / GNU c++13.3.0 / ARCH_ENABLE_CUDA=OFF。
使用既有build-cpu，cmake --build build-cpu --target ARCH -j8；
53个Ninja步骤成功，当前Driver/Runtime/Init/IO、重力依赖和全部模型对象重编。
没有重配WSL/CUDA baseline或重新configure完整ARCH环境。

## 二进制/输入身份
CPU ELF SHA f82bb7ff16c4acf54ae84970b9b403dce3d0370a468d241169519b3bfd1f6a44；
size=7400456。source/header/model编译进入同一完整executable，不以此前manual fixture ELF冒充。
592个tracked src/simulation/include/cmake及CMakeLists/Presets的保守source context digest：
09d2e41cdf415e46a7535765d46c03e748fa24a02097af0e944e650fe02813aa。
这是保守源码上下文，不声称精确转递compiler dependency set；完整逐文件SHA留本机。
文档随后提交的HEAD不是本次ELF编译源码HEAD；不得冒充binary provenance。

## 受影响实际executable回归
9/9 PASS，总59.51秒：
preview_api_contract / preview_full_model_contract /
configuration_api_contract / configuration_entry_contract / configuration_v3_contract /
preview_parameter_metadata / preview_cellular_2d / case_inspection_contract /
preview_session_contract。
OMP_NUM_THREADS=2、CUDA_VISIBLE_DEVICES空；沿现有Core tests，不添加新CI矩阵/改变测试阈值。
这些验证配置/注册模型Init/字段/接口/错误行为/warm session兼容性，
不代表RZ正式场景CPU科学验收、CUDA通过或冻结相同终点benchmark。
原始ctest.log/source-input fingerprints留studio/.local/integration/cpu-rz-source-rebuild-20261004。

## 保持的边界
public Grid/config/Preview RZ仍关闭；内部完整接线未据此发布。
JENS一般EOS/AMR候选粗化、RZ AMR角动量传递、有限环近源方法/预算、
GravityBox轴向模型定义、冻结O9输入/终点/预算仍需Core确认或后续实现。
这里只更新build-cpu/bin/ARCH，未偷偷改当前Studio Project Session选用的binary、
另一build tree或Studio Build Manifest；GUI该身份须独立消费并验证。
本轮未执行正式simulation/长轨迹，无Windows/CUDA/push/tag/main merge。
