# Plotfile 扩展的联合验证

本说明承接[Studio 联合交付入口](StudioConfigurationHandoff.zh-CN.md#43-全模型预览与-plt分开验收)。
合作者负责共享 writer、只读查询和 Viewer 的连贯试做，维护者核对科学语义与独立证据。
首批范围是 Sod 一维和笛卡尔二维 AMR；文件组织与界面布局通过实际交付收敛。

## 现有输出与测试范围

当前 `PlotIO.cpp` 收集活动叶块的 FP64 字段、`Grid/x,y,z` 笛卡尔中心及
`Grid/level,morton`。数组顺序是一维 `(block,i)`、二维 `(block,j,i)`、三维
`(block,k,j,i)`，`i` 最快；坐标数组沿同一顺序展平。输出只包含活动叶块，
没有可直接用于总览的被覆盖粗层场值。

生产文件目前记录 `time/dim/geometry`，尚未记录完整发布、来源身份、字段单位／基底、
原生 bounds 和积分测度。本轮新增的是[开发验证入口](../../validation/backend/plotfile_contract.py)
及[小型反例测试](../../tests/tooling/validation/test_plotfile_contract.py)，生产 writer 未变。
[验证记录](../../validation/backend/results/plotfile-contract/README.md)区分真实输出与合成扩展副本。

这个入口消费候选 JSON 语义和 HDF 路径映射，检查一个指定原生单元。
它不是 Studio 的正式 API，也不规定最终文件格式。`schema_version=1` 仅标识这里的
候选适配格式，与软件版本、配置 API 和 checkpoint 格式版本分别管理。

## 科学语义

| 内容 | 需要明确的约定 |
| --- | --- |
| 完成发布 | 文件标记和实际关闭／发布流程共同成立；文件存在不代表可读完毕 |
| 来源身份 | 实际 case、原始 config、build、运行 binary、EOS；未知值有原因，可信期望来自文件外的受控记录 |
| 字段 | 单位、cell／face 位置、物理含义和分量基底；保留原始值与 FP64 |
| 原生网格 | 活动叶单元 bounds、数组顺序、层级／块标识及 Core 积分测度 |
| Inspector | 回查原生叶单元存储值；显示采样或 LOD 色图值不能替代 |

笛卡尔一维测度为 $\Delta x$，单位 cm，表示每单位横截面积；二维为
$\Delta x\Delta y$，单位 cm²，表示每单位横向长度。
因此 $\sum\rho_i V_i$ 分别是 g/cm² 和 g/cm，不能直接标为三维总质量。
最终 writer 的测度应取自 `GridMetrics::CellVolume`。

当前 `ENTR=P/\rho^{\Gamma_1}` 是压力／密度代理量，不能标为热力学比熵或统一采用
erg/g/K；其单位依赖 $\Gamma_1$ 的约定。`ENER` 为总能量密度，`ENUC` 为比燃烧能率。
新增字段的名称、单位和含义应与生产实现一起核对。

曲线坐标随后单独验收：原生轴、角度、低维归一化和局部正交分量基底必须明确。
`Grid/x,y,z` 来自原生坐标中点的笛卡尔转换，不能用这些点反推曲线单元边界。
当前二维曲线网格是极坐标平面；RZ 迁移需配套 Core 实现与度量验收。
本轮候选探测器明确拒绝曲线坐标和三维候选文件，旧三维文件只做结构识别。

## 可调整的候选适配

默认候选 header 是根属性 `plotfile_candidate` 中的 JSON；也可映射到一个标量
UTF-8 dataset。下面展示一种替代布局，HDF 路径并不是最终命名要求：

```json
{
  "header": "/Metadata/json",
  "data": "/Fields",
  "bounds": "/Cells/bounds",
  "measure": "/Cells/measure",
  "coordinates": ["/Centers/x", "/Centers/y", "/Centers/z"],
  "level": "/Blocks/level",
  "morton": "/Blocks/key"
}
```

`data` 指向包含各字段 dataset 的分组。二维 bounds 的 shape 为
`(block,ny,nx,2,2)`，末两轴为 `(x/y,lower/upper)`；measure 与场 shape 相同。
坐标为三条展平数组；level／morton 各为一条 block 数组。
点选索引包含 block，不能按 Morton 数值排序后直接索引字段。
这里的逐单元 bounds 便于构造反例。生产格式也可复用块区间、分辨率和步长，
在查询时沿 Core 共享网格数学得到单元边界／测度；这种紧凑表示需配套读取适配与独立读回，
不要求重复存储整套逐单元边界数组。

候选 JSON 的最小声明如下；字段声明必须与 `data` 分组一致：

```json
{
  "schema_version": 1,
  "publication": "complete",
  "dimension": 2,
  "geometry": "cartesian",
  "axes": ["x", "y"],
  "storage_order": ["block", "j", "i"],
  "active_leaf_only": true,
  "time": 0.25,
  "identities": {
    "case": {"state": "unknown", "reason": "Prototype identity not recorded"},
    "config": {"state": "unknown", "reason": "Prototype identity not recorded"},
    "build": {"state": "unknown", "reason": "Prototype identity not recorded"},
    "binary": {"state": "unknown", "reason": "Prototype identity not recorded"},
    "eos": {"state": "unknown", "reason": "Prototype identity not recorded"}
  },
  "cell_measure": {"unit": "cm^2", "normalization": "per_unit_transverse_length"},
  "fields": {
    "DENS": {"unit": "g/cm^3", "centering": "cell", "basis": "scalar", "meaning": "mass_density"}
  }
}
```

已知身份使用 `{"state":"known","value":"实际身份"}`。调用者可提供可信期望
JSON，如 `{"binary":"已核对的二进制身份"}`；未知或不一致的声明会失败。
仅比较文件中的字符串不能证明生产程序新鲜度或场值来源。

如果对方已经采用分散属性、逐块 bounds 或其他格式，先提交准确 SHA 和小型字段映射。
维护者在读取适配层归一这些语义，再复用同一组反例；不需要为了本测试副本复制一份
生产 JSON 或改写既有布局。新增适配必须另测实际存储顺序与原生单元读回。
候选中的单位拼写用于反例匹配；等价单位文本可在适配层归一，原始场数组保持其 CGS 值。

## 运行与判定

在含 NumPy／h5py 的工作环境中，从仓库根目录执行：

```bash
python3 validation/backend/plotfile_contract.py \
  --file /path/to/local-candidate.h5 --layout /path/to/layout.json \
  --cell 3,4,5 --field DENS \
  --expected-identities /path/to/trusted-identities.json
python3 -m unittest discover -s tests/tooling/validation -p 'test_plotfile_contract.py'
```

一维索引为 `block,i`，二维为 `block,j,i`。省略索引时只探测首单元；
省略字段时取首个声明字段。输出是一份 JSON：

| 状态／退出码 | 已检查的范围 |
| --- | --- |
| `candidate`／0 | 候选声明、结构和所选单元的局部一致性 |
| `legacy`／2 | 现行旧文件结构；缺失语义保持未知，不读取完整场 |
| `invalid`／1 | 非法、损坏、不支持、缺项或与可信身份不一致 |

单元检查采用端点舍入传播的预算比较中心与测度，不引入物理小值下限。
`logical_payload_bytes_read` 仅计请求的科学数据，不能当作磁盘读取量、chunk 解压量或峰值内存。
header 大小检查也不是整个 HDF5 库的硬内存保护。
这些限制应和“单元一致性不证明全域 AMR 无遗漏／重叠”一起保留在验收结果中。

测试生成的小型 HDF5 全部留在临时目录。新检查由既有 tooling discovery 收集，
不增加 CI 工作流／矩阵，也不要求 FLASH、Studio 或第三方 Viewer。

## 下一份交付

合作者继续完成小范围 writer → query → Viewer 链路，提交准确源码 SHA、字段映射、
只读点选摘要和发布失败检查。临时文件应在关闭成功后于同一文件系统原子发布；
磁盘满、关闭或 rename 失败需要向调用者传播，不推进成功输出序号。
Reader 的 complete 标记反例不能替代这些生产写入验证。

Viewer 再提供总览／缩放／平移／原生单元点选、Native AMR 与 Displayed LOD 区分，
以及实际读取、内存、缓存与同机运行影响的测量。固定返回像素数不保证首次扫描量小。
维护者先冻结已通过 review 的小切片，再扩大字段、几何和三维显示。

提交脚本、小型结构说明、处理后的指标／图表和具体 finding。
原始 H5、plt、checkpoint 与完整数组保留本机；生产文件布局及 UI 布局继续由试做收敛。
