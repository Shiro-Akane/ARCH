# Plotfile 读取状态和取消可达性修复

## 实际finding和修改

承接c26335d0原生finding：长source evidence与图形区域把唯一Cancel按钮滚出视口。
只修改ProjectPlotfileAudit.tsx/CSS，把已有路径、Read metadata、Cancel和status组成sticky栏。
同一滚动容器中始终可见；不添加第二请求状态，不改AbortController/sequence/viewport revision。
没有改变字段数组、文件、Config、Core或物理门槛。

## 自动检查

331/331 Studio/Host PASS、0skip；lint/typecheck/production build/diff-check PASS。
同一套件包含真实pending HTTP断开、受控SIGSTOP reader回收和后续读取恢复，
处理后计时见Summary；该自动检查不是原生GUI worker取消验收。
已有Vite大chunk提示保留，未借机重构。
日志仅在本机ignored .local。修改后完整检查一次，native后没有再重复不变baseline。

## 原生Linux production验证

独立窗口41420456，managed clean64b0ce2f、ELF d53501ae...、
Build92d429da，完整dependency freshness保持unknown。
复用字节相同既有CellularDet H5，time0、20×16×16、5120leaf cells、32×24LOD。
滚动到query/source区后sticky path/status/Cancel真实可见，不必回到页面顶部。

首次尝试停顿下一owned Node child时，该进程在目标点击前出现：
立即按精确PID/startTicks恢复，不当作reader取消PASS；异常过程记录留本机。
修正注入方式：按launcher parent、exe和startTicks确认Host1638876，SIGSTOP Host。
真实点击Read global LOD，界面busy/Cancel enabled且已有成功LOD保持。
真实点击可见Cancel后busy false、显示“Read cancelled; previous successful data retained”。
SIGCONT精确Host后同一取消状态保持；再点LOD，新请求成功。
这是Host处理前pending HTTP的native取消/保留/恢复证明，
不是运行中reader进程回收native证明，也不覆盖全部并发race或refinement淘汰。
首屏观察到Init preparing/current，未点击Generate，不能声称zero Preview活动。

## 清理和边界

正常close exec33054 exit0；8个捕获owned PID/startTicks均退出。
H5 SHA和config bytes未变、managed工作树clean，无Core Build/simulation/push/tag。
仅提交代码及处理后摘要，原始H5/配置/日志留本机。
native active-reader取消、viewport race、剩余模型与独立科学CPU/Jeans/RZ/CUDA/O9仍未闭合。
