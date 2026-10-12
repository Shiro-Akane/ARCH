# ARCH Studio 视频 UI 反馈与修改清单（2026-10-10）

## 分析方法及覆盖

源视频：Desktop 2026.10.10 - 17.08.31.04.mp4，309224487 bytes；
SHA-256：06baec3ccb31461a895f52aeb28855ed8da8884863a6bfb81eaef7de3e73d5ae。
ffprobe：容器716.458667s（11:56.46），视频/音轨各716.416667s，1920×1080/H264/60fps/42985 frames，AAC stereo48kHz。
完整音轨解码为16kHz mono PCM；本地Paraformer/VAD/punc识别26个30s窗口、2s overlap，覆盖0–716.4373125s，无失败或空段。末端约20ms来自解码padding。
时间戳为真实分段范围，不是词级对齐；下表区间按对话所在窗口与对应画面标注。完整转录和模型/hash保留本机。
画面全片每10s检查一次，共72张；结合所有26段对话检查展开、滚动、分组、坐标字段、底栏和参数打开语义。关键点可回查原始帧，不能用单帧代替整段操作证据。

ASR误词按可见控件消歧：grade=Grid，convict=Configure，part= .par，primiter=Parameter，seliler=CellularDet。
坐标映射不能凭发言猜测，应消费当前Core coordinate contract。对话有叠声/口语重复，不伪造逐字引用。
未可靠辨认的英文词不作为需求（例如02:40前后“compromise”）。全部12分钟对话已读，最后结束共享段亦覆盖。
本轮采用音轨识别+对应画面核对；不声称词级人工转录。关键含糊处以两侧重叠窗口、画面和准确转述保守处理。

## Git 前置检查

origin=https://github.com/Shiro-Akane/ARCH.git。
fetch后的compute/optim：e69a859705da0efc0c7abad64c8949f9e4934951。
旧Studio集成工作树：branch studio/compute-optim-integration，HEAD b7cb8b69845d44d6cb0ec4d6b19f4aabb4c65b14，clean。
merge-base=b7cb8b69，旧线0 ahead/71 behind；compute/optim确实包含旧Studio checkpoint，生产frontend/Host/Desktop完整，历史Phase记录移入docs/archive，无需再merge/cherry-pick。
既有worktrees保留，不删除、不reset/stash/clean。
原本地验收branch studio/video-ui-review-20261010及tag studio-video-ui-2026-10-10（0915439eda3d36c8fd6ea0be019c07b093b6f08c）从e69a8597创建，起点clean，仍保留。
发布review branch studio/ui-video-review-20261010重新从同一当前origin/compute/optim创建，移入本轮UI差异无冲突，不携带历史分支额外提交。
Node24.21.0 / npm11.19.0，独立npm ci通过，不复用旧node_modules。

## A. 完整反馈时间线

| 区间 | 画面/操作 | 对话要点与分类 | 问题/实际建议 |
| --- | --- | --- | --- |
| 00:00–00:58 | 顶部workspace selector及标签 | 明确要求：后两项是测试遗留；名称不用反复写real | 主选择只保留Config/Plotfile，测试示例退出正常入口；合并重复标签 |
| 00:28–01:26 | CASE/CONFIG与下方Current Model/Parameter File | 明确要求：定位重复，倾向上方显示真实模型和参数文件 | 顶栏显示CellularDet等实际Model和实际.par；来源细节不再常驻重复横栏 |
| 01:24–02:22 | 左侧展开Grid，向下滚动，再点击上方分类按钮 | 明确要求：分类不随参数滚走，随时能收起 | 固定分类导航在参数滚动区顶部；点击控制对应组，不无意义跳回最顶 |
| 02:20–03:46 | Case组与金色All Custom Parameters分别展开 | 明确要求；讨论中纠正：重叠是区域/功能，不是里面的参数 | 合为一个Case parameters区域，保留模型schema参数和真正custom键，不能删XH等参数 |
| 03:44–04:14 | Runtime及Grid坐标系字段 | 明确要求：Runtime最常用应靠前 | Runtime排首，Grid其次；原组内工作流次序沿用 |
| 04:00–04:42 | Grid下部Coordinate system、滚回上部axis | 明确要求：先选坐标系再填轴参数 | geometry移至Grid顶部、axes之前 |
| 04:40–06:06 | x1·x/x2·y轴标题与坐标解释 | 明确要求：x1+x重复、不够直观 | 标题只显示Core权威物理轴名（X/Y或Core提供的r/角度），raw x1/x2键保留在详情，不猜柱/球映射 |
| 06:04–07:02 | 底部Configure/Build/Run/Restart与Show terminal | 明确要求：横向排开；terminal更大更明显。可选建议：永久展开 | 主操作同一底栏横排，Terminal放相邻显著位置；保持可收起，不强制永久开 |
| 07:00–08:26 | blocks/dimensionless及domain/cm说明 | 明确要求：没有量纲不必反复写1/dimensionless；真实cm和角度单位要保留 | 无量纲/不适用单位退出常驻行；物理单位保留，详细说明hover/详情可达 |
| 07:56–09:22 | 每字段详细文字、unit not applicable、unknown | 明确要求/接受建议：文字过密，低价值重复未知提示可减少 | 描述/raw key/来源/长说明放可键盘访问详情和tooltip；不能隐藏验证错误或真实依赖 |
| 09:20–10:18 | 左栏宽度讨论 | 未实施的一致延期：先不急，属于额外UI缩放功能 | 本轮不做resizable panes或新增缩放系统 |
| 10:16–11:42 | Open Config、Model dropdown与Parameter File | 明确问题：Open Config让人以为是加/选模型，实际上选.par；建议命名分开 | 改为Open Parameter File (.par)；现有模型选择标明Model，不新增任意C++模型加载系统 |
| 11:40–11:56 | 结束共享 | 一般总结/闲聊 | 没有新增UI功能要求 |

## B. 实施和验收清单

以下为针对实际建议的实现选择，不是整套重设计。P0没有新增科学功能；错误、保存和身份的保留是全部项的共同验收门槛。

| ID/优先级 | 原反馈/区域 | 选择的修改方案及预期体验 | 交互变化 | 验收标准 |
| --- | --- | --- | --- | --- |
| V01/P1 | 00:00–00:58 测试workspace/real冗余 | 正常选择只有Config与Plotfile；测试样本保留调试入口，退出主工作流；浏览器默认Config | workspace入口呈现 | 正常模式无Mock/Cellular snapshot选项；真实Plotfile仍可打开；调试示例不冒充Core结果 |
| V02/P1 | 00:28–01:26 顶部重复身份 | CASE实际模型、PARAMETERS实际文件；完整来源/Host路径/未证实association保留可展开详情 | 展示分层 | 模型与.par同步，不能据文件名宣称verified；Core/Build身份仍可查 |
| V03/P1 | 01:24–02:22 分类导航滚走 | 分类导航sticky，可随时展开/收起；可见按钮与展开状态一致 | 滚动/分类 | 展开长组滚动到底仍能操作导航；收起不跳到无关面板，不丢字段 |
| V04/P1 | 02:20–03:46 Case/custom分裂 | 一个Case parameters承载schema模型参数、真正custom和缺失model metadata键；分小区标明来源 | 区域合并 | 原所有参数可搜索编辑；同一key只出现一次；retired不进Custom；schema未来参数仍可达 |
| V05/P1 | 03:44–04:14 Runtime靠后 | Runtime首位，再Grid/EOS/Network/Gravity/Diffusion/Case，未知schema组继续追加 | 顺序 | runtime常用项第一组可达，组内既有排序不回退 |
| V06/P1 | 04:00–04:42 先填轴再问geometry | Coordinate system置于axes上方 | 顺序 | geometry在首轴之前；变化仍使用Core坐标contract和last-valid布局 |
| V07/P1 | 04:40–06:06 x1+X重复 | 物理轴名一次显示，raw key/axis identity仅详情 | 文案 | Cartesian/curved标签来自Core；x3 off和临时非法token仍稳定，不猜映射 |
| V08/P1 | 06:04–07:02 底栏挤两行 | Configure/Build/Preview/Run/Restart/Terminal形成主操作行；状态与history独立 | 布局 | 1920/1280桌面按钮完整可达；终端可开关；关drawer不取消task；执行/确认语义不变 |
| V09/P1 | 07:00–09:22 过多说明 | 每字段显示label/control/真实物理unit；描述、raw key、default/source等移到help/details；不常驻重复unknown/dimensionless/not-applicable | 信息层级 | Help可键盘访问，三层值不混淆；路径失败、错误、retired/forbidden明确可见；Inspector raw仍真实 |
| V10/P1 | 10:16–11:42 打开参数文件被误认选模型 | Open Parameter File (.par)，Load Project Parameter File；现有Model选择明确，补tooltip | 文案 | 打开仍只.par、未保存替换确认/取消保留；不新增Open Model API |
| V11/P2 | 06:04–07:02 terminal永久显示提议 | 采用更清楚的可展开Terminal入口，不永久占画布 | 可选实现 | 有界日志仍保留，关后不中断Build/Configure |
| V12/P2 | 09:20–10:18 调宽 | 明确延期，记录不实施 | 无 | 不新增pane拖宽、手机UX、UI缩放系统 |

## C. 简化与完整能力

- 顶部只显示当前模型/文件一次，完整来源、Build freshness与association未知仍可查，不伪造Current。
- 分类导航固定；Case/custom合一区域，保留全部schema/auxiliary/case declarations和未知raw键。简化不删除功能。
- Runtime先、geometry先；物理轴标签消费Core坐标说明，不自行推断科学含义。
- 重复单位/长说明退出常驻字段行，真实cm/rad/code_*等权威单位仍显著。键盘help与折叠详情保留完整信息。
- 错误、retired/forbidden removal、未保存/磁盘冲突、Preview stale/unsupported和AMR limited/mismatch不因折叠消失。
- 保持当前共同线自动Preview/取消/迟到结果隔离语义，不恢复已归档Phase的限制。
- 保持Undo、Save/Save As、Build日志、Preview、AMR、Inspector与Plotfile全部能力。

## D. 范围和待辨明内容

明确要求：V01–V10。讨论可选实现：V11。明确延期：V12。
实现者补充仅限帮助控件键盘可达、桌面溢出修正、必要的对应回归测试。
右侧Inspector/中央plot在讨论中被评价“没大问题”，不借机重构；不改plot数值、采样或科学逻辑。
最后“Open model / Open parameter file”的发言作为入口命名建议处理，不当作新模型装载接口授权。
本轮不改Core科学源码、物理定义、动态schema默认值、Parser/round-trip、原始response arrays，UI实施阶段未自动push/merge或进入新Phase；后续用户已授权发布独立review branch/PR，仍不合并PR。

首次只读审计未发现共同线现成matching binary/manifest。实施后已在新工作树建立真实CPU Release build，binary SHA-256=240a4f573631c76daf45797ba7b0f95512e3050d2bb67f7e9f2131b4e9b47d23，并经Host标准Build取得真实Manifest（buildId=799b719d-4bd6-4381-8fab-b3ddd33e5602）。tracked inputs一致，完整dependency freshness仍UNKNOWN并如实显示。旧b7cb binary未被冒充为e69a来源，未手工生成Manifest。详细UAT和限制见同目录ARCH_STUDIO_VIDEO_UI_IMPLEMENTATION_REPORT.md。
