# 运行时 EOS 表

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

状态方程（EOS）表为模拟提供热力学数据，例如密度、温度与能量之间的关系。
应选择与参数文件中的 EOS 相匹配的表；其他数据集使用的规范化 HDF5 格式见
[表格接口](../src/physics/eos/TabularEOS.zh-CN.md)。

本目录只保存纳入版本控制的运行时 EOS 数据。生成器、探索性表格和上游源码快照不放入运行时数据树。表格按模型或数据集归类：

```text
EOS_toolkit/
├── README.md
├── README.zh-CN.md
└── tables/
    └── helmholtz/
        └── helm_table.dat
```

未来用于生产的表格应放入 `tables/<模型或数据集>/`，不得直接放在 `EOS_toolkit/` 根目录。每张新表必须记录来源、schema、checksum、再分发条款以及至少一项验证记录。参数文件使用相对于仓库根目录的路径或绝对路径，例如：

```ini
eos_type = helmholtz
eos_table_path = EOS_toolkit/tables/helmholtz/helm_table.dat
```

需要特别说明的是，`helm_table.dat` 文件提取自项目下载的 Timmes `helmholtz.tar.xz` 压缩包，并严格通过 Git LFS 进行管理。由于 Git LFS 会将大表数据单独保存，而普通 Git 检出中仅保留一个指向该数据的小指针文件，因此在运行任何 Helmholtz 算例之前，必须先下载 LFS 的实际数据。关于该表格的详细来源、重新分发条款、必需大小要求以及校验和（checksum），请查阅 [`THIRD_PARTY_NOTICES.zh-CN.md`](../THIRD_PARTY_NOTICES.zh-CN.md) 以及 [`validation/burn/README.zh-CN.md`](../validation/burn/README.zh-CN.md)。
