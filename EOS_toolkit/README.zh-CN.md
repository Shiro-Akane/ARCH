# 运行时 EOS 表

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

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

`helm_table.dat` 来自项目下载的 Timmes `helmholtz.tar.xz`，通过 Git LFS 管理。来源、重新分发范围、必需大小和 checksum 见 [`THIRD_PARTY_NOTICES.zh-CN.md`](../THIRD_PARTY_NOTICES.zh-CN.md)及 [`validation/burn/README.zh-CN.md`](../validation/burn/README.zh-CN.md)。
