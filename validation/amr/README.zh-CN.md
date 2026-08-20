# AMR 验证状态

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

> 状态：保留定性证据；CPU 和 CUDA 定量验收待完成。

现有 AMR 档案已纳入此页，使验证状态能从统一索引发现。图像展示 Sedov 爆炸、扩散 Gaussian、带重力/扩散的 Rayleigh–Taylor 流动和 cellular 燃烧的叶 patch 分布与场结构。它们不含已提交的均匀网格参考、L1/L2 范数或容差，因此只属于诊断。

| 算例 | 可见模块 | 当前证据 |
| --- | --- | --- |
| Sedov | 流体、激波驱动细化 | [图像](../../Validation_file/AMR_Visual_Archive/sedov_amr.png) |
| Gaussian | 扩散、移动细化模式 | [图像](../../Validation_file/AMR_Visual_Archive/gaussian_amr.png) |
| Rayleigh–Taylor | 流体、重力、扩散 | [图像](../../Validation_file/AMR_Visual_Archive/rt_amr.png) |
| Cellular burn | 流体、燃烧 | [图像](../../Validation_file/AMR_Visual_Archive/cellular_amr.png) |

![Sedov AMR 诊断](../../Validation_file/AMR_Visual_Archive/sedov_amr.png)

## 仍需完成的定量记录

下一份 AMR 基线将把每个 AMR 输出与采用 AMR 最细间距的均匀网格比较，并报告：

- 公共网格上的体积加权 L1/L2；
- regrid 前后总质量、动量、能量和核素漂移；
- reflux 前后的粗细通量不匹配；
- restriction/prolongation 守恒以及常数/线性场测试；
- 穿越细化界面的光滑特征；
- 使用相同细化决定的 CPU/CUDA 拓扑和场一致性。

PPM 当前在粗细面使用 MUSCL-MinMod，因此不接受 AMR 全域三阶空间收敛声明。历史 renderer 和输入 manifest 保留在 `Validation_file/AMR_Visual_Archive/`；此状态页不需要新的分析脚本。
