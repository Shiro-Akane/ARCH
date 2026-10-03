# plt 元数据有界原型进度

基线 41aa5354a497a7cab5aacac9b20849a0dd773238；遵循联合交付入口 §4.3。
本步是允许的读取准备原型，不是正式结果 Viewer，不宣称 plt 独立出口完成。

## 实际 writer 与边界

src/io/plot/PlotIO.cpp 从 active leaf interiors 收集场和 Grid::GetPhysicalCoords，
src/io/hdf5/HDF5Writer.cpp::write_hdf5_plt_impl 直接 Create/Truncate 最终文件。
time/dim/geometry 为属性；Grid/x/y/z 是展平 Cartesian 单元中心，
Grid/level/morton 每叶块一项；Data 形状为 [blocks,Nx] / [blocks,Ny,Nx] /
[blocks,Nz,Ny,Nx]，x1 最快。该 writer 没有原子 rename 或完成标记；
部分字段写入后失败仍可能留下可打开 HDF5。不能根据打开成功、大小稳定、
mtime 或字段可读就推断完成。当前 writer 也未保存单位、原生单元边界/体积、
case/config/build/binary/EOS 身份；Reader 不补猜这些信息。

## 本步实现与证据

studio/host/plotfileMetadata.ts 仅提供本地审计函数，没有 server route 或 UI 接入。
Linux descriptor 固定读取对象，拒绝 symlink/非 regular/空文件及大于64MiB输入；
逐字段只读取 dtype/shape，最多128字段、每dataset最多1000万项，核对
共同场shape、三条coordinate长度和两个block metadata长度。
源 SHA-256 分块计算；stat变化拒绝。只读取三个 scalar 属性，不读取 field arrays。
外部字段链接不跟随，缺项/不支持结构明确失败；没有 EOS、Driver 或 CUDA调用。

结果始终 completion=unknown、renderEligible=false，字段单位、科学身份、
原生单元 bounds/volume 为 null/unavailable。文件摘要证明选中对象身份，
不是 writer completion 证明。非方形 [2 blocks,3,5] 已覆盖。
真实维护 Sod fixture 和先前获授权的 t=0 12叶块输出读取成功；
后者192 cells、time=0，依旧不能凭文件自身认定正式完成。
维护 fixture 字节未变；原始输出保留本机 ignored 目录，没有上传场数组。

5/5 scoped tests、lint、typecheck PASS。测试将 Dataset.value getter 设为抛错，
证明元数据路径不会访问场payload；同时覆盖shape冲突、coordinate不匹配、
外部链接、字段数/文件大小预算、symlink、invalid/empty及错误后恢复。
invalid HDF5 测试的库诊断是预期拒绝，不是测试失败。
此次没有前端改变，不重复未变化的UI生产UAT，也未重编 ARCH。

## 正式接入前交由 Core review 的问题

1. 最终文件通过哪个 authoritative completion 约定发布？建议由 IO owner
   明确成功close/flush后同目录 atomic rename 的语义、失败处理及覆盖策略；
   如采用manifest/marker，须定义文件SHA绑定及写入顺序。此处未修改writer。
2. 旧文件可否依带文件SHA的已完成 Run manifest 明确接纳？不要以mtime/打开成功替代。
3. 当前与未来 RZ 的字段单位、Cartesian center 与 native coordinate关系、
   leaf logical identity、native bounds/spacing/volume 如何保存或关联？
   单靠morton与center不能让Reader无依据地重建完整物理单元。
4. config/build/binary/EOS身份缺失时，是否允许明确标 unknown 的 legacy显示？
   不得与当前项目或当前Preview错误绑定。

## 后续实现

冻结上述语义后再做 Host-owned isolated reader（超时、内存、取消/子进程回收）、
视口/LOD传输和原生单元Inspector，并进行真实桌面结果验收。
目前64MiB/字段/shape限制只是原型拒绝预算；不是对任意恶意HDF5解析耗时/RSS的
硬隔离保证。此函数尚无 production request入口，不能直接作为正式服务器reader。
既有浏览器小型1D H5入口是历史功能，不计入本轮独立plt出口验收。

精简证据：PlotfileMetadataPrototype.Summary.json。
完整检查log与metadata JSON留 studio/.local/integration/plotfile-metadata-audit。
无新simulation、无科学IO改动、无Windows适配、无push/tag/main merge。

## 隔离读取增量（2026-10-03）

基线 cf743de8d37ef7862f9721ab2c15025a02b8fb69。
Host 新增 inspectPlotfileMetadataIsolated，固定使用当前 Node 和仓库内
plotfileMetadataWorker.ts；无 shell、不接受 program/args/env。
路径仅作为本地读取参数，本步没有新增 browser endpoint 或项目文件授权入口。

HDF5/WASM 解析和哈希在单独进程内执行，Host 不导入 HDF5 runtime。
每个 Host 进程最多一个读取；默认15秒、允许Host缩短但不可延长。
stdout上限64KiB，超限/取消/超时SIGKILL，并等待close后才清除active、settle请求。
预取消不启动worker；失败后下一次读取可以成功，不能释放槽位却遗留旧worker。
Node heap上限256MiB并非WASM/整个进程的硬RSS限制，不宣称内存绝对隔离；
正式大文件入口仍需真实平台资源预算与更完整文件授权。

新增五项隔离测试：真实既有Sod只读、预取消/坏路径恢复、
超时/在途取消/并发拒绝、非法Host timeout、真实长字段名HDF5触发输出超限后恢复。
这些测试覆盖进程生命周期；在途取消测试不宣称已中断特定解析阶段。
结合现有五项metadata检查与上一轮视口边界新增项，完整Studio/Host255/255 PASS，
typecheck、lint、production build、diff-check PASS；测试后worker进程扫描为空。
完整日志在ignored .local/integration/full-model-production-20261003/plt-isolation-*。

未接入UI/Host endpoint，未读取field payload，未修改Core writer或生成simulation输出。
completion仍unknown、renderEligible=false，单位/科学身份/native bounds-volume
仍缺authoritative contract；本增量不等于正式plt查看器交付。
没有push/tag/main merge，没有CUDA或Windows适配。
