# Plotfile live HTTP断开清理验证

基线 86800bb74c2e122e0f0c9679b37b2603ba399d69，Linux/WSL lane。
新增host-plotfile-disconnect.test.ts，经实际loopback Host、Project-owned
维护Sod H5及固定metadata/slice worker验证，不用Mock worker或固定sleep冒充active。

通过/proc当前测试Node的直接child列表、精确worker argv、parent和start身份找到
真实live reader；复核后SIGSTOP，观察同身份T状态，100ms后HTTP仍未settled。
然后AbortController断开真实HTTP连接，worker消失后下一次metadata返回200，
SHA一致，fixture字节不变。metadata与slice分别覆盖。
本轮定向实测回收约14.897ms和9.678ms；这是观察值，不是新的性能预算。

覆盖为 **显式worker stall故障注入期间的HTTP disconnect**，
不声称自然HDF5解析某一具体指令正在执行、正式GUI取消或窗口关闭。
固定worker的child close容量释放与真实HTTP路径共同通过。
测试清理仅允许核对同parent/start/argv的owned PID，不操作其他进程。
首次/proc退出竞态ESRCH导致验证工具失败，日志保留；修正仅识别ENOENT/ESRCH
为该读取对象已不存在，不吞掉其它错误，生产代码未修改。

定向1/1及最终完整273/273、lint/typecheck/diff PASS。
production source/bundle未改，不重复已通过build或ARCH编译。
原始日志仅本机ignored studio/.local/integration/plotfile-http-disconnect-20261003/。
没有simulation、CUDA、raw上传、push/tag/main merge。

HTTP审计生命周期已补证；正式Plotfile UI/LOD/native单元及完成/科学身份契约
仍待推进，renderEligible=false和completion unknown保持。
