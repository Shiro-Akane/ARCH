# 原生活动 AMR 请求关闭：受阻共享 worker 清理通过

2026-10-03，基线2deb281005da746df86f7048b8c301f7e9784a69，工作树clean。
复用当前Linuxproduction窗口19270674与既有4096-cell保存Sod输入，
不改变参数或重跑此前AMR科学对照。原生收起已完成Configure日志，拖动
工作区滚动条到Initialization & Initial AMR；此前滚轮输入未改变可见滚动位置，
没有据此重复请求或修改配置。Generate initial AMR预算保持512blocks/128MiB。

先确认Preview succeeded、workflow非running，核对本次Host76933的唯一
直接 --preview-session worker77360/startTicks1038828、真实binary路径。
明确SIGSTOP故障注入只作用该idle共享worker，90秒同PID/startTicks/T状态
SIGCONT保护防止遗留暂挂。未暂停Host/launcher或已交付计算。

原生Generate initial AMR一次，请求9ba1e717-a55f-481c-a37d-37e97af5cd10。
GET workflow/status持续preview-amr/running，06:44:22.053Z再次直接复核；
worker仍T、保护未恢复。原生窗口显示Working/preparing和Cancel initialization。
状态接口尚无result identity，未把baseline Preview identity冒称本次AMR响应身份，
也不声称本次已经完成Core AMR数学计算。

随后原生点击Studio关闭按钮：
06:44:35.035Z shutdown requested；06:44:38.701Z owned Host exited；
06:44:38.704Z desktop clean shutdown。
/proc监测及关闭后复核确认Electron76887/Host76933/worker77360均消失；
90秒保护没有恢复worker，未通过手动kill或Host mutation代替原生关闭。
没有收集worker退出码或精确内部终止阶段，不根据总耗时伪造SIGKILL退出证据。

**未完成AMR请求原生关闭与owned清理PASS（明确fault injection scope）。**
不是自然AMR计算中的关闭时序证明，也不新增真实hierarchy数值验收。
此前已通过的独立AMR对照继续按原报告覆盖范围使用。

保存配置SHA9edb45ef29ce5bbf371f9145f5bc1dbb051ee31563472aaee865e159e59b3d16；
binary SHAe506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7。
两者前后不变。无Run/Restart、Save、Build、Core修改/CUDA；
原始监测/保护脚本仅在studio/.local/integration/native-stalled-amr-close。
只提交精简身份/状态/时间，JSON与diff check通过；
不重复未变的234项回归，无raw上传/push/tag/main merge。
当前窗口已关闭；3C文件覆盖验收及阶段收束仍未完成。
