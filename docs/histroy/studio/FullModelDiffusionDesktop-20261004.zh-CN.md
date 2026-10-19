# DiffusionMode Linux production desktop 定向验收

## 真实来源与范围

Studio launch source 46f3865d47e9e6a54f58cc5b951180aa591a753e；
managed source 64b0ce2f8d97553f59024978618f1e4848a974e1。
复用已通过 production assets 和独立 clean CPU ELF：
d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75，
Build ID 92d429da-42d9-4336-b956-79c43440b5c3；完整依赖 freshness 仍 unknown。
本次只做 Init/initial AMR/native Inspector，不做 Configure/Build、Save、扩散演化或科学预算变更。
配置逐字节复制已有合法输入，身份见同名 Summary.json。D_spec=0，
不能据此宣称 diffusion solver 验收。配置原注释/base_name/out_dir 源于 Sod 模板，
真实 case 以 launcher/registry 的 DiffusionMode 为准，不根据文件名猜测。

## 原生 UI 验证

独立窗口9634356。先观察 registry unavailable，再观察 preparing/current；
本轮未点击 Generate Real Preview，因此不记为手动首次请求通过，也未重复请求。
512 个一维样本、x1=[0,1] cm、Density 恒1 g/cm^3；
字段菜单仅 DENS/PRES/TEMP/VELX/ENER/EINT。
图上点选 index123：x显示0.24121094 cm，sample-center算术123.5/512=0.2412109375；
rho=1、P=1、T显示0.0034843206 K、VELX=0、ENER=EINT=2.5。
这是 UI 舍入与配置一致性证据，不是独立科学 oracle。

状态区 EOS ideal/ready，Species 展开显示 index0=background、index1=tracer。
真实 Setup 注册这两个热力学属性相同的组分、Init 赋互补 mass fractions。
当前 UI field menu 和 sample Inspector 没有组分质量分数；本轮只证明组分身份可见，
不把组分空间场显示或余弦模式数值验收标为 PASS。

Generate initial AMR 后 Current/Complete、8leaf 全L0、128active cells、
completedPasses0、配置容量32/工作容量32；无 preview limit。
资源估计 base36864 / with-species49152 / pool147456 bytes，speciesCount2；
resource estimate 不作 OOM prediction。
选块0:5:0:0：level0/logicalIndex[5,0,0]、bounds[.625,.75] cm、
cellShape16、spacing.0078125 cm，与 (1/8)/16 算术一致。
API 无 AMR cell field array；field 初态 snapshot 的 hierarchy-not-constructed 与独立 AMR 结果分开。

## 清理和未完成项

正常关闭 exit0，8个 owned PID+startTicks 全部消失；
输入SHA不变、managed output/sod_standard 不存在、managed working tree clean。
本轮没有源码修改，不重复不变的自动检查，没有 wheel/pan input。
原始输入/launcher日志在本机 ignored .local；仅提交处理后的摘要。
组分场显示仍未完成/未证明；完整模型矩阵、native wheel/pan、科学CPU/Jeans/RZ/CUDA/O9仍未闭合。
