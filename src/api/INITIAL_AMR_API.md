# 初始网格、资源提示与本地模型查询

本扩展沿用单进程、stdin `.par` 文本、stdout 单个 JSON。外层 `schemaVersion=1.0`，AMR 与模型发现扩展版本为 `1`。不启动 HTTP 服务。入口由 `--preview-capabilities.extensions.amr/discovery` 发布；旧顶层 `amrHierarchy=false` 继续说明旧的 `--preview` 是规则点采样。新客户端按扩展协商。

## 模型查询

```sh
ARCH --list-cases
```

返回 `kind=registered-cases`，`cases` 来自本 binary 的注册表，不构造模型、不调用 Setup。每项提供注册名、场预览和 AMR 预览支持状态、支持维数；当前实际网格仅支持 Cartesian Sod 1D、CellularDet 2D（shock_dir=0/1）。注册成功不等于任意模型已支持预览。

此查询给本地启动器使用。`arch-studio` 命令、桌面窗口、项目发现、源码/构建身份以及文件管理由 Studio/Host 实现。Core 不依据 `.cpp` 文件名猜测注册名，也不能通过该列表证明源码与 binary 一致。

## 资源估算

```sh
ARCH --amr-resources Sod --config-stdin < simulation/Sod/Sod.par
```

不调用 Setup、不加载 EOS、不构建网格。caseId 仅为请求上下文；用于一般 1/2/3D 配置的规模提示。返回 `kind=amr-resource-estimate`，`data.levels` 列出 0 到 lrefinemax：

- `fullDomainLeafBlocks`：全域升至此级的块数，B0 × 2^(dimension × level)。
- `activeCells`：全域活动单元数，不含 ghost/padding。
- `baseStateBytes`：目前三份状态、每份六个 double 数组，计入 ghost/padding 的基础存储。
- `stateBytesIncludingSpecies`：Setup 前为 null；实际网格响应内已有组分数量时可计算。
- `overflow`：任一存储计算超过 int64 时为 true，对应数值为 null，不能当零。

`poolPreallocatedBaseBytes` 另列当前 `.par` 的池容量基础预分配；`max_blocks<=0` 使用 Core 原有的 10000 回退。数字是当前单进程布局下单份全域数据的规模，未包含 EOS、临时数组、树和迁移计划等；不是运行峰值或 OOM 预报，也不按节点数/rank 数分摊。

## 实际初始 AMR 网格

```sh
ARCH --preview-amr Sod --config-stdin --request-id mesh-001 \
  --mesh-max-blocks 512 --mesh-memory-mib 128 < simulation/Sod/Sod.par
ARCH --preview-amr CellularDet --config-stdin \
  < simulation/Cellular/CellularPreview2D.par
```

本轮有界工作进程支持 Linux/WSL CPU。非 Linux 返回明确错误，不退回无保护的大规模分配。CUDA 不初始化。可执行文件每次只接受一份请求。

`--mesh-max-blocks` 默认 512、范围 1–1024；`--mesh-memory-mib` 默认 128、范围 16–256。这是预览工作预算，不写回 `.par`。不接受 `--samples` 或二维采样参数。输入 1 MiB、响应 8 MiB、UTF-8 和身份规则沿用既有接口。

实现通过共享根网格初始化、EOS 细化量计算、物理边界与 ghost 交换、`AmrTree::Regrid` 的指标/2:1 平衡/保守迁移建立真实初始叶块。没有时间推进、反应推进、CUDA、plotfile、checkpoint 或 sidecar。新的细块由 Core 保守插值得到，不在细化后重新调用 Init 覆盖其数值。

`kind=initial-amr-preview`；`data.kind=amr-leaf-mesh`。主要字段：

| 字段 | 含义 |
|---|---|
| leaves[].logicalKey | level:i:j:k；本次配置中的逻辑身份，不是可复用的池下标 |
| leaves[].level / logicalIndex | 真实层级和三方向逻辑索引 |
| leaves[].lower / upper | 活动方向的块物理边界，cm；不含 ghost |
| leaves[].cellShape / cellSpacing | 块内各方向活动单元数和间距；GUI 据此生成单元线 |
| levelCounts / leafCount | 当前返回快照的各级叶块数及总数 |
| complete / completedPasses | 是否完成 Core 所需的初始细化；已完整执行的判断/细化轮次 |
| snapshot | last-completed-balanced-hierarchy，或根网格预算不足时的 none |
| limitedReason | 预览限制原因；完成时为 null |
| configuredMaxBlocks / workingCapacity | `.par` 的有效容量与本次较小的预览工作容量 |
| resources | 包含实际已注册组分数量的资源规模表 |

相邻共面叶块维持 2:1 平衡。缩放只改变绘制，不改变叶块或重新细化。图形可叠加旧 `--preview` 的规则场采样，但要说明它是 Init 采样，不是 AMR 单元平均值。当前 API 不传 AMR 单元场数组。

达到网格预算时：退出码仍为 0、顶层 `status=limited`、`data.complete=false`。若根网格能放入预算，返回最后一次完整平衡快照；否则 `leaves=[]`、`snapshot=none`。前端不得将 limited 显示成完成，也不能用空网格覆盖旧图而不提示。错误时沿用非零退出码，不能把失败数据当作新预览。

### 预算和进程边界

池容量在构造前按配置容量、块数预算和存储工作预算取最小值。存储预算按三份含组分状态的四倍加每块 64 KiB 预留估算；这是保守工作规模规则，不是 RSS 上界。每次细化先完成指标和平衡判断，再在分配新子块之前检查迁移时旧块与新块共存的容量。停止发生在完整细化之间。

CLI 在 Setup/EOS 前收紧进程地址空间为至多 1 GiB、CPU 时间为至多 30 秒，并用单个 OpenMP 线程。地址空间限制不是可用物理内存检测或部署建议；继承到更严的系统限制时不会提高它。网格循环另有 10 秒检查点预算。

Host 必须继续实现墙钟超时（建议 45 秒）、取消和子进程回收。遇到系统终止、超时、损坏/缺失 JSON 时，保留上一次有效图并显示本次失败；进程被杀时不保证有最终 JSON。不要为重试自动增加预算或改写科学参数。

## 验证

2026-09-21 CPU Debug 构建与 15 组 scoped CTest 最终通过；详细结果见 [LOCAL_WORKFLOW_HANDOFF.md](LOCAL_WORKFLOW_HANDOFF.md)。

新增 `ui_expansion_contract`：模型枚举、90 参数展示、扩散依赖、CGS、资源公式与溢出、根网格/细化预算、1D/2D 平衡和域覆盖、CUDA 请求仍走 CPU、非法请求。

另用小配置令正式 CPU Driver 在 t=0 初始化并输出临时 checkpoint，比对 Sod 与 CellularDet 的逐块逻辑身份和层级。该测试不进入时间推进；测试夹具的临时文件与 API 本身不生成文件的约定分开验证。
