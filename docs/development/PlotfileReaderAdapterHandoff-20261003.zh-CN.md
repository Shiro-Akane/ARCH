# Plotfile 读取适配对接：已实现候选切片

## 精确引用和范围

对接验证 contract：23ff77c4f08419de2b3c5eadee214da2af25784e（只读获取，未 merge）。
当前 writer/query/Viewer 完整源码切片：5b6d89e48d33cb7caf7129d0f81992f06ebb773c，分支 studio/compute-optim-integration。
此 SHA 是实现引用，不是原始 H5 的 binary build 身份。t=0 参考文件由 build-cpu executable
a5d3467297188775069d9c466cdfacb0dfa3143ec7a834de714446c12a248308 生成，
构建时源码 24f448ce；完整构建记录见 PlotfileRealT0Progress-20261003.zh-CN.md / PlotfileRealT0-20261003.Summary.json。
本交付限 Sod 1D、Cartesian CellularDet 2D 活动叶块；非曲线/3D 合格声明。

## HDF 路径和数组映射

| 语义 | 实际路径 / 属性 | shape / 顺序 |
| --- | --- | --- |
| 时间、维数、几何 | 根属性 time / dim / geometry | 标量 |
| 发布声明 | 根属性 plot_publication_version=candidate-1、plot_publication_state=complete、plot_publication_method=checked-close-atomic-replace | 声明需与真实发布流程共同检查 |
| 原始场 | /Data/<field> | FP64，1D [B,Nx]；2D [B,Ny,Nx]，i 最快 |
| 笛卡尔中心 | /Grid/x、y、z | FP64 [N]，与场 C-order 展平顺序相同 |
| 原生 cell bounds | /NativeGrid/x1_lower、x1_upper；x2、x3 同名规则 | FP64 [N]；inactive bounds 均 0 |
| 积分测度 | /NativeGrid/cell_measure | FP64 [N]，来自 GridMetrics::CellVolume |
| 层级、旧 Morton | /Grid/level、morton | [B]，文件 block 顺序；不按 Morton 排序索引 |
| 文件内逻辑标识 | /NativeGrid/logical_x1、logical_x2、logical_x3 | uint32 [B]，与 level 组合；不是跨文件永久身份 |
| 部分来源证据 | /SourceIdentity 的属性和 species_names dataset | 下节说明 |

全局索引：1D b*Nx+i；2D (b*Ny+j)*Nx+i，均排除 ghost。
适配到候选 bounds 时，将 x1/x2 lower/upper 组合并 reshape 为 [B,Ny,Nx,2,2]；
测度 reshape 为场 shape。适配不修改科学场值，也不要求生产 writer 复制一份候选 JSON。
NativeGrid 属性：candidate-cartesian-1、cell、ghost_cells=0、active-leaf、
center_basis=cartesian、measure_source=GridMetrics::CellVolume、
measure_convention=active-coordinate-product; inactive-measures-omitted。
当前 measure_unit=unknown，必须保留这个缺口，不能擅自转成已验证 cm/cm²。

## 字段语义和单位缺口

现有 writer 没有逐字段 unit/meaning/basis 属性。读取适配不能仅凭字段名把未知声明视为已发布科学 contract。
实际实现位于 src/io/plot/PlotIO.cpp；保留原计算、FP64 和 checkpoint 语义。

- DENS：rho。
- PRES / TEMP：现有 EOS 回调，使用该单元实际组分。
- ENER：FluidState.eng，总能量密度。
- VELX/Y/Z：原始动量 / 密度；首批 Cartesian 分量，不推广到曲线基底。
- ENTR：P/rho^Gamma1 代理量，不标热力学比熵或固定 erg/g/K。
- ENUC：现有 enuc_rate；按对方 contract review 为比燃烧能率。
- VORT / DIVV：共享 VelocityDiagnostics；species 使用既有组分字段，名称由文件实际字段及 ordered species 对照。
- 字段按 config 输出开关产生；读取 /Data 枚举真实列表，不硬编码每个文件一定含全部字段。

下一步 writer 需加入受 review 的字段 unit/basis/meaning，以及低维 measure normalization；
未知单位必须明确保留。1D rho*measure 不能标三维质量，2D 同理。

## 来源身份

/SourceIdentity：version=candidate-identity-1、scope=partial。
case_id 来自 ConfigurationInput.case_id；raw_config_sha256 是实际解析输入字节（不是重读磁盘）；
binary_sha256 来自 Linux /proc/self/exe，仅 main-executable-only；
eos_type / eos_table_sha256 / ideal_gamma / ordered species_names 来自已解析 runtime checkpoint provenance。
ideal EOS table 标 not-applicable。run_id、effective_config_sha256、build_id、source_git_head、
eos_unit_system 仍 unknown；根 plot_identity_state=unknown 不是“完整身份已验证”。
可信预期必须来自文件外已核对的输入/构建记录。查询文件 SHA 是内容身份，不等于科学来源身份。

## 发布与失败

同目录 .partial-XXXXXX → 写入 → flush → checked H5Fclose → atomic rename；
异常向调用者传播，删除可删除的临时文件，保留先前正式文件，成功日志只在发布后打印。
没有 fsync，不宣称断电持久性。已测 write/flush/close 注入、rename 失败及发布前 SIGKILL；
SIGKILL 可留下 partial，reader 拒绝该名称。实际磁盘耗尽/硬件掉电仍不是已完成验证。

## 查询、显示与证据边界

只读 Host query：metadata、raw slice、global display LOD、physical point、explicit viewport LOD。
物理点选择用原生 bounds（half-open，global maximum inclusive），无匹配/重叠多匹配明确失败。
Inspector 返回原始存储值和原生 cell 身份；LOD 为 coordinate-overlap-weighted display mean，
不是原生值、科学积分或覆盖粗层场。native outline 来自实际叶块，最多显示 128 个并声明 limited。
zoom/pan/Fit 仅重绘；viewport refinement 显式请求。过期/取消/失败保留最后成功结果。
首次总览和 viewport refinement 仍扫描所有叶块并计算文件 digest；固定像素不保证扫描少。
没有实际空间索引或跨查询 cache；缓存磁盘 read_bytes=0 不表示无逻辑读取。
成本工具 validation/io/measure_plotfile_query.mjs 报 response bytes、wall、CPU、maxRSS、rchar/read_bytes；
maxRSS 含 Node/WASM/import，并非增量内存，rchar 非纯 HDF dataset bytes。

已完成 302 项 Studio/Host 回归及 lint/typecheck/build；真实 t=0 DENS 对照：
Sod 192 cells、CellularDet 5120 cells 与 checkpoint bit-exact，中心/测度差 0；
Cellular 单元 bounds 对 Preview 最大差 1.7763568394002505e-15，记录为 review finding，
不自行新增科学容差。block extent 对照差 0 不替代这条单元 finding。
仅 t=0、DENS 切片，不证明演化或所有字段一致性、全域无遗漏/重叠。

参考脚本：validation/io/verify_initial_plotfile.py、verify_plotfile_blocks.py、measure_plotfile_query.mjs。
处理后报告见 docs/development/Plotfile*20261003*；原始 H5/plt/checkpoint/完整数组留在 studio/.local。
当前新 production Linux 桌面已启动，实际 Viewer 交互 UAT 尚未完成，不把 HTTP/SSR 算作 native UAT。

## 适配方下一步

可直接针对上述 SHA 的分散属性布局归一到 23ff77c4f 的验证语义；
先测试 block,j,i 索引、FP64、原生 bounds/measure 和可信外部身份匹配。
没有逐字段声明时返回具体 missing finding，不以名称补齐“已知”声明。
完成单位/基底及完整身份 review 后冻结小切片，再扩展几何和更大文件。

## 对接引用更新：原生桌面增量验证

最新实现引用：4e9a364081e097271b2988bd5ce1ec86b864f3b8。
HDF writer/字段和数组映射保持上文布局；后续增量主要修正只读 Desktop 启动 gate、
一维场值轴留白、空间导航和控件对比度，未改变 t=0 文件或 binary 身份。
305 项 Studio/Host regression、lint/typecheck/build/diff check 已通过。

实际 Linux production 桌面已完成 Sod 与 CellularDet 的读取→总览→缩放/平移/Fit→
原生 point Inspector 小切片；两个 point 用独立 h5py 核对 FP64 值、bounds、measure。
最新一维导航修复亦已复验，显式 viewport refinement 后 Fit 能恢复原始全域。
具体步骤、finding 和证据边界见 PlotfileNativeDesktopFindings-20261003.zh-CN.md；
processed point 见 PlotfileNativeDesktopPoint-20261003.Summary.json。
尚未完成所有 field switch、wheel、cancel/race/failure native UAT 和大文件/索引/缓存测量，
因此不是完整 Viewer 封箱结论。

交给读取适配层时，应以最新实现 SHA 核对代码，同时独立核对 t=0 file SHA / binary SHA，
不能把当前 source HEAD 当成旧参考文件的构建来源。
来源完整性和逐字段 unit/basis/meaning 仍按 missing finding 处理；ENTR 代理量、
低维 measure normalization 以及 Cellular per-cell bounds 舍入 finding 继续由维护者 review。
原始 H5/plt/checkpoint 留本机；本地分支尚未 push，不能将该 SHA 描述成远端可访问交付。
