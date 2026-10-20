# Plotfile 运行实例身份补齐

基线 148c0d06ca884bba4f2251cd5d4226a28801b2b2。落实 owner
PlotfileValidationContract 的来源身份要求；scope 仍 partial，不认证完整provenance。

## 生产接线

Linux core/files/RunIdentity.h 使用 getrandom 取得16字节OS entropy并格式化UUIDv4。
重试EINTR，短读继续填满；无法获得entropy明确失败，不退回PID/时间/路径。
DriverIO 首次真正写Plotfile时生成并持有一个ID，写失败重试保持同一ID，
后续输出沿用；新run_simulation/DriverIO实例，包括Restart调用，拥有新ID。
无输出时不获取entropy。不改变物理输入、推进、原始场数组或checkpoint格式/状态。

write_plt 显式传入ID，SourceIdentity增加run_id_source：
DriverIO output session; OS-generated UUIDv4。run_id为实际UUID或unknown。
writer发布前严格校验格式；bad identity不替换已有正式文件。
Host/client消费UUID+来源组合，拒绝错误UUID/错误来源/来源缺失；
旧candidate unknown与无SourceIdentity文件继续兼容。
Viewer显示单独Run output session，effective config/build/source Git保持unknown。
这只是输出会话标识，不是Studio Host run ID、checkpoint恢复链或binary新鲜度证明。

## 验证

CPU标准构建成功，plotfile_publication 1/1 PASS（含既有发布失败检查）。
Studio npm test 321/321 PASS；lint/typecheck/build PASS，保留既有大bundle警告。
新增writer UUID生成/读回/非法输入保持正式文件测试；
HDF → Host/client UUID及反例8项scoped测试PASS。

使用已有明确批准CPU tmax=0/max_steps=-1输入，仅改out_dir；
Sod与Cellular各运行两次，全部退出0，四个UUID各不相同。
Sod9字段、Cellular28字段每一完整FP64数组对既有文件bit一致；
每次20个checkpoint numeric datasets dtype/shape/bytes一致。
四份实际文件经production isolated point与client验证，
run/raw config/binary身份匹配外部记录，源文件SHA不变。
没有进入演化；没有修改/删除旧输出。raw/ELF/logs留本地ignored。

本次binary SHA 13e8c786b4c5c47e50c7a77f8f78c9f757c117a4af845e01d0e55f90f789b97d。
从148c0d06基线+dirty inputs构建，逐文件hash见Summary，
不将后续文档提交HEAD冒充编译来源。桌面build-studio-cpu binary未替换。

工具：
validation/io/verify_plotfile_run_identity_t0.py --binary <archived CPU> --references <local JSON> --output-root <new local directory>
node --experimental-strip-types validation/io/verify_plotfile_run_identity.mjs <local summary.json>

本地证据 studio/.local/integration/plotfile-run-identity-20261004；
处理后的准确input/output/binary身份见同名Summary。

## 尚未覆盖

每次真实t=0只产生一个Plotfile；同run多个输出的ID稳定性与Restart新ID目前
由DriverIO生命周期代码保证，未运行新的演化/Restart对照，不作该类实测声明。
本轮未做native desktop UAT、CUDA、真实ENOSPC、同机演化影响或push/tag。
effective-config/build/source Git没有权威完整生产记录，继续unknown；
UUID不能替代科学单位/基底/积分、独立oracle、完整AMR或全目标验收。
