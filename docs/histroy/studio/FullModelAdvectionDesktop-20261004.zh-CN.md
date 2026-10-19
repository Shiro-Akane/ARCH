# SmoothAdvection Linux 原生初态／AMR 验证

处于联合计划第5项全模型初态／AMR。新开 production Electron + owned Node Host，
从01371b1e4f0dc5e9b9fad57a73cd154fb7a3339f启动，
managed project 为独立 clean ARCH-host-clean-build-20261004，HEAD64b0ce2f。
CPU binary SHA d53501ae231578fa6f270539ccada1af9bc049bc7d499b047a101332bbd4aa75，
Build92d429da-42d9-4336-b956-79c43440b5c3。完整依赖新鲜度仍unknown，未升级为verified。

原生窗口32574480；非browser/Vite。原有效SmoothAdvection.par逐字节复制到本机ignored
UAT目录，SHA374077d75ca66534f3b10ca6bf7cd6ebed8ce758e619b081271336116fb71212。
旧模板Sod注释/output名称未修改；真实case由explicit SmoothAdvection注册身份提供，
不能从文件名、base_name或注释推断模型。未Run/Restart/simulation。

## 已验证

真实1D Init 512samples，domain x=[0,1]cm；密度曲线min0.800024/max1.19998g/cm³。
原生点击命中sample2，x=.0048828125cm，rho1.0061343，P1，VELX1；
其余UI舍入值见Summary。只能作为显示/命中证据，不是独立科学oracle。

真实InitialAMR Complete，无preview limit，0passes，8leaf全部L0；
configured max_blocks32、working capacity32不变。
原生outline条带与独立初态曲线分开；资源表128activecells，
scale estimate明确不作OOM预测。
列表选择logicalKey0:7:0:0、logicalIndex[7,0,0]、level0，
bounds[.875,1]cm，cellShape16，spacing.0078125cm；
独立算术7/8与(1/8)/16精确符合显示。
AMR cell field arrays明确“Not provided by this API”，不得将右侧Init sample叫AMR cell value。

## 尚未通过的输入验证

一次标准scrollY=-120后，应用plot范围未变、背景截图内容改变。
这支持input routing异常的怀疑，不能确定因果或证明应用wheel handler有缺陷。
停止重复wheel；不操作背景、不修OS、不新增绕过按钮。
源码显示wheel由SVG处理，canvas在下层；普通点击与app scrollbar拖动成功。
zoom/pan保持UNVERIFIED，不因曲线正常或数学单测而标PASS。

## 关闭与范围

正常点击owned窗口X；launcher exec session91594 exit0。
8个owned PID/startTicks全部消失，无未知进程kill，其他用户ARCH终端未操作。
input SHA与原副本一致，configured output/sod_standard不存在，managed worktree clean。
原始日志留ignored UAT目录，不提交截图/全数组/H5/checkpoint。
本轮未修改源码，不重复不受影响的329/329 Studio baseline。
全模型native矩阵、wheel/pan、待Core确认的科学出口与CUDA/O9仍未完成。
