# 物理终点 runner 本地日志流式写出

## 问题与修改

原 one_run 使用 capture_output，将完整 stdout/stderr 保存在 Python 内存中，
进程结束后才拼接写入 run.log。批准的长轨迹可能产生大量日志；
这既增加父进程内存占用，也使执行期间缺少本地错误线索。

现在为每个已受保护的新 run directory 创建二进制 run.log，
将子进程 stdout 与 stderr 直接重定向到同一个文件描述符。
Python 不保留完整输出副本，也不做文本解码转换。
两条输出流按实际写入到达日志的顺序合并；不再是先全部 stdout、后全部 stderr。
此改动不强制改变 ARCH 自身的 stdio 缓冲行为，也不是断电持久性保证。

非零退出仍立即停止当前 campaign，不启动后续 backend 或自动重试。
完整日志保留本地；异常诊断最多读取末尾 65536 bytes、显示其中最后 35 行。
诊断的 UTF-8 替换只用于显示，原始日志字节保持不变。
无法启动 executable 时，保留 execution.json 的 launch_error 与 elapsed_seconds；
不虚构 launch_exit_code，也不 fallback。

## 检查与身份

- 31/31 原终点/冻结输入/affinity/三路调度及新增日志测试 PASS。
- 真实 Python 子进程写出超过 1 MiB stdout、含非 UTF-8 的 stderr，
  退出 7：原始字节长度与内容完整，末尾错误可见，首部不进入有界诊断。
- 成功路径验证文件描述符传递、执行期间写入可读取；
  不存在的 executable 保留错误和空日志，不重试。
- 原 taskset 失败测试夹具起初仍通过 CompletedProcess.stderr 返回输出，
  首轮 30/31；更新夹具为向实际 stdout 描述符写入后 31/31。
  首轮日志仍保留，没有以生产 fallback 迁就夹具。
- architecture audit 与 diff check PASS；处理后摘要见
  [summary](../../validation/gravity/results/physical-endpoint-stream-log-20261005/summary.json)。

复现：使用现有 NumPy/h5py venv 执行：

    python validation/gravity/curved/test_physical_endpoint.py -v

完整日志在本机 studio/.local/integration/physical-endpoint-stream-log-20261005。

production CPU ARCH SHA256 仍为
7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510。
没有修改科学 Core、运行 ARCH、重新构建或重复冻结 JENS 9+9。

## 未覆盖

本节点只解决长跑日志的内存与失败证据保管，不代表资源峰值采样、
实际 production OpenMP team、CPU 最优线程选择、CUDA 身份或计时验收。
冻结科学输入、完整 CPU 出口、统一 CUDA 与批准长轨迹仍按原计划推进。
没有修改终点、科学阈值、物理定义、配对顺序、预热或 Driver 计时含义。
原始场与全量日志不上 Git；不开展 Windows 适配。
