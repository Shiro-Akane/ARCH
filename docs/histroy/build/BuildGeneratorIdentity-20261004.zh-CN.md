# 3C build generator 身份：只读审计

## 当前实际缺口

此前 LTO retention 已在真实 Host CPU Build 保留172 linker inputs，missing从97降为0；
这不等于完整dependency closure。现有 Manifest的CMake files/编译器/GNU subprogram/linker
证据尚未记录固定Host CMake与cache中实际Ninja的binary身份及前后稳定性。
不是再次实验已解决的LTO临时文件，也不把mold archive失败混同retention结果。

## 本轮动作

新增validation/io/audit_build_generator_identity.py，只读：
- 核对cache source/build binding与当前Host Ninja generator；
- 固定Host /usr/bin/cmake与CMAKE_COMMAND严格一致；
- 指纹CMAKE_MAKE_PROGRAM，path/realpath/size/SHA，读前后与symlink target检查；
- cache2MiB、单工具128MiB预算；任何缺失、相对路径、重复键、非executable、
  unsupported generator或source/build错绑明确失败；
- 从不执行cache中的program/command或configure/build，也不读取环境秘密。

实际build-cpu与build-studio-cpu均通过；CMake SHA
1c5227af4edd22d8d689def545e18ee458260c0fd579eba2187967f38817e638，
Ninja SHA5965527e09fe2b3787772aa4f711d6a36b393e7f2fcaa744a7a96c5a4ddf59cb。
工具及cache身份在同名Summary.json，observedExecution=false，不冒称观察到actual exec。
4项tests包括content变更、duplicate/source binding/generator、missing/relative/nonexec/
Host mismatch、cache超限，以及带touch payload的cache工具不执行。

## 复现及下一步

python3 validation/io/audit_build_generator_identity.py --source /home/arch/projects/ARCH-compute-optim --build /home/arch/projects/ARCH-compute-optim/build-studio-cpu
python3 -m unittest discover -s tests/tooling/validation -p test_build_generator_identity.py

下一步将同类Host-owned generator evidence纳入Manifest，并比较构建前后与freshness刷新。
旧Manifest缺少该证据必须保持unknown；工具身份不全或不稳定不能假称current。
仍须审计generator runtime dependencies及完整closure，不能仅增加两个hash置
dependenciesComplete=true。现有profile和freshness-unknown未改。

本轮原始日志留studio/.local/integration/build-generator-identity-20261004，
未运行科学程序/重build/改变Core/CUDA/Windows/push/tag。
CPU science blockers保持，不以该工程审计替代数值验收。
