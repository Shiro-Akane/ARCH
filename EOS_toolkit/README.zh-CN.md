# 运行时 EOS 表

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

状态方程（EOS）表为模拟提供热力学数据，例如密度、温度与能量之间的关系。
应选择与参数文件中的 EOS 相匹配的表；规范化 3D/4D HDF5、原生 EOSDriver
总 EOS HDF5，以及 Shen EOS2/EOS4 使用的原始正温度重子 ASCII 主表格式见
[表格接口](../src/physics/eos/TabularEOS.zh-CN.md)。

本目录只保存纳入版本控制的运行时 EOS 数据。生成器、探索性表格和上游源码快照不放入运行时数据树。表格按模型或数据集归类：

```text
EOS_toolkit/
├── README.md
├── README.zh-CN.md
└── tables/
    ├── helmholtz/
    │   └── helm_table.dat
    └── baryon/
        ├── eos2.tab
        └── eos4.tab
```

未来用于生产的表格应放入 `tables/<模型或数据集>/`，不得直接放在 `EOS_toolkit/` 根目录。每张新表必须记录来源、布局、校验值、再分发条款以及至少一项验证记录。参数文件使用相对于仓库根目录的路径或绝对路径，例如：

```ini
eos_type = helmholtz
eos_table_path = EOS_toolkit/tables/helmholtz/helm_table.dat
```

来源表可以保存在仓库之外。将现有 tabular 策略直接指向受支持的来源文件即可，
不要求拷贝到本目录，也不要求转换出另一张表：

```ini
eos_type = tabular
eos_table_path = /absolute/path/to/eos2.tab
use_burn = false
# 可选：默认使用以下已有表，仅在缺少电子时读取。
eos_helm_table_path = EOS_toolkit/tables/helmholtz/helm_table.dat
```

加载器读取来源声明的物理分量，或采用已识别原生格式的明确成分契约。对于部分
自由能表，只在加载时补齐缺失的电子／正电子和光子，保留原有重子模型。总表或
仅缺光子的表不需要电子表。规范化 HDF5 制表方通过 `eos_components` 声明组成，
可选 `baryon_mass_g` 记录固定的来源质量约定；不会按数值猜测缺失成分，也不为
每种 EOS 增加物理调参文件。

[接口指南](../src/physics/eos/TabularEOS.zh-CN.md)规定固定能量基准、F/P/S 约束、
无效来源／补齐模板的屏蔽、严格温度反解，以及核平衡与燃烧限制。可以载入不代表
整个表域都已取得科学精度资格。任意 CompOSE 布局，以及零温／零电荷的辅助
ASCII 表，不属于当前正温度主表读取器的支持范围。

## 原始 Shen 数据

作者的 [EOS2/EOS4 归档](https://zenodo.org/records/3612487)允许按 CC BY 4.0
重新分发其中已识别的原始文件。ARCH 通过 Git LFS 随附两份未修改的主表成员，
位于以下位置：

| 运行时数据 | 字节数 | 解压后原始成员的 SHA-256 |
| --- | ---: | --- |
| `tables/baryon/eos2.tab` | 143,166,842 | `d52d37d30fec10ffb5279689a172e61a7ebb3538a4acaf2270dc32469c0d3c58` |
| `tables/baryon/eos4.tab` | 143,167,115 | `5ee37819f873387af9c38207bcada72a48abe695b9491df9ad5c6a5e69487c34` |

两份解压后的表合计 286,333,957 字节。使用前须下载 LFS 实体内容；只有指针的检出
不能用于查询 EOS。也可以使用同一精确成员的外部路径。解压不会改变表中采样值
或许可。署名为 H. Shen、
F. Ji、J. N. Hu、K. Sumiyoshi，*Equation of state for simulations of core-collapse
supernovae and neutron-star mergers*（2020），
[DOI: 10.5281/zenodo.3612487](https://doi.org/10.5281/zenodo.3612487)。
须保留[许可与来源说明](../THIRD_PARTY_NOTICES.zh-CN.md)。上游六文件归档另含
`.t00`、`.yp0` 产品；归档包含它们并不代表当前读取器支持这些文件。

加工后的 HShen HDF5 数据不随附。其 EOSDriver 格式经过兼容性调查，但须另行
核对加工产品发布方的条款；原始归档的 CC BY 许可不会重新许可这些加工产品。

需要特别说明的是，`helm_table.dat` 文件提取自项目下载的 Timmes `helmholtz.tar.xz` 压缩包，并严格通过 Git LFS 进行管理。由于 Git LFS 会将大表数据单独保存，而普通 Git 检出中仅保留一个指向该数据的小指针文件，因此在运行任何 Helmholtz 算例之前，必须先下载 LFS 的实际数据。关于该表格的详细来源、重新分发条款、必需大小要求以及校验和（checksum），请查阅 [`THIRD_PARTY_NOTICES.zh-CN.md`](../THIRD_PARTY_NOTICES.zh-CN.md) 以及 [`validation/burn/README.zh-CN.md`](../validation/burn/README.zh-CN.md)。
