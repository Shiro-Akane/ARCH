# SNIaCoupled Linux production desktop 定向验证

## 来源和边界

Launch source a69886ee64af22b8227e654c04ac7b3057784fb1，production assets 代码65aed733；
managed clean source64b0ce2f8d97553f59024978618f1e4848a974e1。
CPU ELF d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75，
Build92d429da-42d9-4336-b956-79c43440b5c3；完整依赖freshness仍unknown。
复用既有合法输入，SHA见Summary。本轮没有Configure/Build/Save/Run；
只覆盖初始化、共享EOS转换和初始AMR界面，不覆盖燃烧/扩散/自引力演化验收。

## 真实界面观察

独立窗口45877974，128×128、shape[128,128]、x1-fastest，x/y=[0,1]cm，x3=0cm。
启动观察期间Preview自行变为preparing/current；本轮未点Generate，不记手动首次请求PASS。
Density/Temperature热点可见；sample8127、i=j=63、x=y=.49609375cm，
显示rho10099932、P1.8871271e24、T2.9986441e9、VELX/Y0、ENER4.1283780e24、EINT4.0875304e17。
这些是UI舍入记录，不是独立EOS科学oracle。Temperature range显示1.00004e9–2.99864e9 K。
切换字段清空sample selection，Preview保持Current。菜单7个fluid fields，没有连续组分场或GPOT/GAC/ENUC。

点击Inspect initialization后Current，9个raw probes；本轮实际打开首probe(.25,.25,0)cm，
未逐个验收全部9个。raw DENS10006217.652402211、TEMP1124353048.0442326，
PRES0明确unused，TEMP为conversion consumed input。
13个组分按Core index/name展示；he4及除c12/o16之外的组分1e-20，c12/o16各.5。
输入xhe4=0与raw Init的Core floor值区分；UI不额外归一化、clamp或添加默认值。
显式/缺省/effective来源保持区分；这张稀疏probe表不是连续composition field。

## 初始AMR与清理

单次Generate initial AMR完成Current/Complete，1leaf L0、0passes、配置/工作容量16。
选块0:0:0:0：bounds[0,0]→[1,1]cm、cellShape16×16、spacing(.0625,.0625)cm；
spacing与1/16一致。pool base1769472 bytes仅资源估计，不作OOM预测。
overlay标识field/config/build/EOS/native coordinates匹配；没有AMR cell arrays明确可见。
field snapshot的hierarchy-not-constructed与独立AMR结果分别保留。

正常窗口close，exec78847 exit0；8个已捕获owned PID/startTicks均消失。
这一检查覆盖捕获的进程身份，不宣称监控了所有短时worker。
config逐字节未变、配置output不存在、managed working tree clean。
原始输入/日志留ignored .local，仅提交本摘要。没有源码修改，不重跑不变331项基线。
wheel/pan、本项目科学CPU/Jeans/RZ/CUDA/O9和完整模型矩阵仍未闭合。
