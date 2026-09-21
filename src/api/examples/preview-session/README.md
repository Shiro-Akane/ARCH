# 预览会话示例

在项目根目录，使用本轮 CPU binary：

```sh
python3 -B src/api/examples/preview-session/client.py build-ui-api/bin/ARCH \
  < src/api/examples/preview-session/requests.ndjson
```

- `requests.ndjson`：Sod 位置修改、越界失败、资源释放和恢复请求。参数保存在请求里，不改写 `.par`。
- `sod-events.ndjson`：上述请求的实际响应，含 ready、progress、result、error 状态和 reset；用于说明协议，不应逐字比较耗时或日志。
- `client.py`：最小 Python 标准库接入参考；保持 stdin 打开、持续读取事件、固定墙钟截止时间、超时/退出清理。正式 Host 还需实现身份校验、输入合并、进程重建和 UI 状态管理。
- `benchmark.py`：重现三组冷/热请求；CellularDet 128×128 场、CooperativeHotspots 九点初始化检查、Sod 实际 AMR。每组使用新会话，不降低采样形状或求解精度。
- `cpu-debug-timing.json`：本机 2026-09-21 实测原始阶段耗时和缓存计数；CPU Debug、CUDA OFF、工作进程一条 OpenMP 线程。不是跨硬件性能承诺，不包含 Host 队列和 Studio 渲染。

```sh
python3 -B src/api/examples/preview-session/benchmark.py build-ui-api/bin/ARCH .
```

首次请求表示进程内未准备资源，不保证操作系统磁盘缓存为空。热点检查只有 9 个真实初始样本，不能与二维完整场图混为同一工作量。几何和温度两个编辑均从同一参考 `.par` 分别修改。

CellularDet 样本中 16,384 个坐标均执行真实 Init，只有 37–38 种逐位不同的转换输入；单次请求内精确复用省去重复 EOS 转换。连续场本身没有重复状态时，仍需逐点执行全部转换。

完整协议、限制和各方分工见 [PREVIEW_SESSION_API.md](../../PREVIEW_SESSION_API.md)，同步信息见 [PREVIEW_SESSION_HANDOFF.md](../../PREVIEW_SESSION_HANDOFF.md)。
