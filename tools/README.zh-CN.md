# 开发与验证工具

[English](README.md) · [验证拉取后的源码](../tests/README.zh-CN.md)

这些工具随源码提供。顶层 Python 入口共同使用执行、比较和身份检查函数，因此保留
在同一目录；网络生成按职责归入 [network/](network/README.md)。科学参考方法与输入
属于 [Validation](../validation/README.zh-CN.md)，这里不维护另一套数学或运行设施。

## 从无需 GPU 的检查开始

使用 Python 3.10 或更新版本，以及构建环境中的 Git/CMake。从仓库根目录执行：

```bash
python3 tools/audit_architecture.py .
python3 -m unittest discover -s tests/tooling -p 'test_*.py'
```

架构审查检查共用实现与头文件边界，Python 测试使用受控输入验证工具行为。两者都
不运行模拟，也不表示物理结果已经验收。完整资源保护测试需要 Linux `/proc`、pidfd、
子进程管理支持，以及 Python 的 `os.pidfd_open` 接口。

无需编译求解器，也可以检查清单与验证协议：

```bash
python3 tools/validate_backend_results.py \
  --manifest validation/amr/gpu_cases.json --unit-test
python3 tools/validate_cuda_amr_restart.py --unit-test
```

这些模式只检查验证协议，不产生 CPU/CUDA 轨迹，不能替代实际场比较。

## 核对 CI 测试报告

[check_ci_results.py](check_ci_results.py) 核对 CPU CI 配置的 CTest 清单，以及可选的
JUnit 结果报告，拒绝关键测试缺失、重复或遗漏结果、失败和跳过。它读取 CTest 的
通过／失败判定，不重新定义数值误差标准。接入方式见[工作流指南](../.github/workflows/README.md)；
这项报告检查不生成科学 Validation 证据。

## 完整程序与结果比较

使用启用测试的构建，并提供同一构建的 ARCH 与 `arch_cuda_single_level_validation`。
CUDA 测试需要可用显卡；稀疏 CUDA 燃烧还需要 cuDSS，并编入所选网络与 EOS 的
执行组合。只有使用自定义网络的算例才需要生成网络包，内置网络也可以使用稀疏求解。
准备方法见[测试指南](../tests/README.zh-CN.md)。

- [smoke_cuda_amr_runtime.py](smoke_cuda_amr_runtime.py)：短时完整程序运行，
  起步说明见[短测指南](../tests/smoke/README.md)。
- [validate_backend_results.py](validate_backend_results.py)：按清单运行 CPU/CUDA，
  比较实际场、拓扑与守恒量。
- [validate_cuda_amr_restart.py](validate_cuda_amr_restart.py)：使用共用 ARCH
  检查点契约，验证严格恢复与后续演化。
- [qualify_cuda_amr_evidence.py](qualify_cuda_amr_evidence.py)：按清单、源码／构建身份
  和最终产物检查已生成的验证记录。

将输出放在本地 build 或临时目录中，目标目录必须不存在或为空。运行器保留日志，
不自动删除前一次数据。工具中“发布证据”指写入本地 JSON，不会上传；维护者审阅
实际结果后，再更新 Validation 中对应的有效记录。

## 构建与内存观测

[run_memory_guarded.py](run_memory_guarded.py)只管理它启动的命令及后代进程。
内存、swap 和可选 Linux PSI 压力检查见[构建指南](../docs/guides/Build.zh-CN.md)。
GPU 指标仅在显式请求时采集，不会预留显存。

[summarize_cuda_compile_memory.py](summarize_cuda_compile_memory.py)读取编译观测，
[validation_device_memory.py](validation_device_memory.py)读取分析器中的进程分配事件。
它们不执行编译或求解器，也不根据配置上限推测性能。

## 共用设施与可选依赖

[validation_provenance.py](validation_provenance.py)统一管理源码／构建／输入身份和
文件替换检查；后端运行器的 `run_arch_with_logs` 统一保存进程日志；
[validation_sanitizer.py](validation_sanitizer.py)管理 Compute Sanitizer 及报告核查。
扩展这些已有职责，不再复制运行器、进程管理或证据格式。

顶层工具只使用 Python 标准库，HDF5 读取交给 C++ 比较器。独立参考可能需要 NumPy、
SciPy、mpmath 或 h5py，各验证模块均有说明。Compute Sanitizer 和 Nsight 只在对应
插桩流程中需要。

[网络生成](network/README.md)需要 pynucastro。`GenerateNetwork.py --check` 会执行
配方并在内存中构造网络，但不写入包文件。运行前请检查配方内容，保留上游数据来源，
并在生成所选网络后重新配置 CMake。
