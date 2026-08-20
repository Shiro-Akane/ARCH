# 表格 EOS 资源

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

运行时 EOS 表按模型集中在本目录：

```text
eos_tabular/
└── helmholtz/
    └── helm_table.dat
```

未来表格应放入含义明确的模型或数据集子目录，而不是 `EOS_toolkit/` 根目录。参数文件必须使用相对于仓库根目录的路径或绝对路径，例如：

```ini
eos_type = helmholtz
eos_table_path = EOS_toolkit/eos_tabular/helmholtz/helm_table.dat
```

`helm_table.dat` 来自项目下载的 Timmes `helmholtz.tar.xz`。它通过 Git LFS 管理，与 `EOS_toolkit/helmholtz/` 中保留的原始 Fortran 源码分开。来源、重新分发范围、必需大小和 checksum 见 [`THIRD_PARTY_NOTICES.zh-CN.md`](../../THIRD_PARTY_NOTICES.zh-CN.md)及 [`validation/burn/README.zh-CN.md`](../../validation/burn/README.zh-CN.md)。
