## N1: 构建与测试入口在 Debug 和 Release 配置下可信

Dependencies: none

- [x] 修改 `CMakeLists.txt`，用默认值为 `OFF` 的 `BUILD_TESTING` cache option（缓存选项）替换 `TINYREDIS_BUILD_TESTS` 环境变量，并保持现有 6 个测试 executable 和 CTest 名称不变。
- [x] 新增 `tests/test_config.h`，在 `NDEBUG` 已定义时触发编译错误。
- [x] 修改 `tests/resp_test.cpp`、`tests/database_test.cpp`、`tests/command_test.cpp`、`tests/server_test.cpp`、`tests/cli_test.cpp` 和 `tests/event_loop_test.cpp`，使每个测试 translation unit（翻译单元）包含 `tests/test_config.h`。
- [x] 修改 `CMakeLists.txt`，为 6 个测试 target 添加 `-UNDEBUG`，使 Release test 中的 `assert()` 条件和副作用都实际执行。
- [x] 修改 `CMakeLists.txt`，为 6 个 CTest 设置 30 秒 `TIMEOUT`。
- [x] 修改 `build.sh`，添加 Bash shebang，并从 `${BASH_SOURCE[0]}` 定位仓库根目录后显式传入 `-DBUILD_TESTING=OFF` 和所选 `CMAKE_BUILD_TYPE`。
- [x] 修改 `test.sh`，从 `${BASH_SOURCE[0]}` 定位仓库根目录后显式传入 `-DBUILD_TESTING=ON` 和 `-DCMAKE_BUILD_TYPE=Debug`。
- [x] 修改 `tests/event_loop_test.cpp` 的 3 个无参数 `EventLoop::wait()` 调用，使每次等待最多持续 1 秒并在超时时失败。
- [x] 修改 `tests/server_test.cpp` 的 `ServerHarness::~ServerHarness()` 和 `ServerHarness::stopWithSigterm()`，通过 `waitpid(..., WNOHANG)` 和 `steady_clock` deadline 在 2 秒内回收 child，并在超时后终止 child 后报告测试失败。
- [x] 修改 `tests/server_test.cpp` 的 `connectToServer()`、`writeAll()`、`readExact()`、`readClosed()` 和 `readClosedOrReset()`，使每个 socket 等待使用 `poll()` 和单一 `steady_clock` deadline。
- [x] 修改 `tests/cli_test.cpp` 的 `writeAll()` 和 `readAll()`，使 pipe I/O 在 2 秒 deadline 内完成或失败。
- [x] Run `bash -n build.sh test.sh`; expect both scripts to pass Bash syntax validation.
- [x] Run `./build.sh release && ./test.sh`; expect the final Debug test configuration to build and all 6 CTest tests to pass within their 30-second limits.
- [x] Run `cmake -S . -B build-release-tests -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build-release-tests && ctest --test-dir build-release-tests --output-on-failure`; expect all 6 Release tests to execute their assertions and pass.
- [x] Run `repo=$PWD; (cd /tmp && "$repo/build.sh" release && "$repo/test.sh")`; expect both scripts to use the repository source and build directories rather than `/tmp`.

## N2: macOS 和 Linux 持续执行同一构建门禁

Dependencies: N1
Blocked by: 将包含 `.github/workflows/ci.yml` 的分支推送到 GitHub，才能执行 hosted macOS 和 Linux runner（托管运行器）验证。

- [x] 新增 `.github/workflows/ci.yml`，在 `push` 和 `pull_request` 上启动 `ubuntu-latest` 与 `macos-latest` matrix，并设置 `fail-fast: false` 和 `permissions: contents: read`。
- [x] 配置 `.github/workflows/ci.yml` 的每个平台 job 使用 `actions/checkout@v4`，依次运行 `./test.sh` 和 `./build.sh release`，且不安装 Redis、不运行 benchmark、不缓存 build。
- [x] 配置 `.github/workflows/ci.yml` 的 job 名称包含 matrix OS，并设置 15 分钟 job timeout，使两个 backend 的状态可独立识别。
- [x] Run `ruby -e 'require "yaml"; YAML.load_file(".github/workflows/ci.yml"); puts "valid YAML"'`; expect `valid YAML`.
- [ ] Run `run_id=$(gh run list --workflow ci.yml --limit 1 --json databaseId --jq '.[0].databaseId'); gh run watch "$run_id" --exit-status`; expect the latest Ubuntu job to compile `src/event_loop_epoll.cpp` and the latest macOS job to compile `src/event_loop_kqueue.cpp`, with both jobs passing Debug tests and the Release build.

## N3: Database 拒绝不可表示的 TTL 和 snapshot 时间戳
Dependencies: N1

- [ ] 修改 `src/database.h`，定义 `SetWithTtlResult::{Stored, InvalidTtl}` 和 `ExpireResult::{Updated, Missing, InvalidTtl}`，并使带 TTL 的 `Database::set()` 与 `Database::expire()` 返回对应结果。
- [ ] 修改 `src/database.cpp`，新增接收显式 `now` 和正毫秒数的 checked-expiration helper（受检过期时间辅助函数），使用商和余数完成 clock period 转换，并在每次乘法与 `now + delta` 前检查目标 representation（表示范围）。
- [ ] 修改 `src/database.cpp` 的带 TTL `Database::set()`，在 `ttl <= 0` 或 helper 失败时返回 `InvalidTtl`，且不修改原 value、TTL 或 `expire_index_`。
- [ ] 修改 `src/database.cpp` 的 `Database::expire()`，先验证 TTL，再查询 key，并分别返回 `InvalidTtl`、`Missing` 或 `Updated`，且失败结果不修改 value、TTL 或 `expire_index_`。
- [ ] 修改 `src/database.cpp` 的 `toEpochMilliseconds()` 和 `fromEpochMilliseconds()`，在 duration ratio conversion（时长比例转换）与 time-point 构造前检查范围，并用显式失败结果替代不可表示的转换。
- [ ] 修改 `src/command.cpp` 的 `SET EX` 和 `EXPIRE` 分支，将 `InvalidTtl` 映射为现有 `ERR invalid expire time`，并保持正常成功、missing 和非正 seconds 的 RESP 回复不变。
- [ ] 修改 `CMakeLists.txt`，仅为 `tinyredis-database-test` 和 `tinyredis-command-test` 定义 `TINYREDIS_TESTING=1`。
- [ ] 修改 `src/database.h` 和 `src/database.cpp`，在 `TINYREDIS_TESTING` 下暴露接收显式 `now` 的 checked-expiration wrapper，使边界测试不依赖 wall clock（墙上时钟）。
- [ ] 修改 `tests/database_test.cpp`，覆盖精确可表示边界、边界外、ratio conversion overflow、直接传入非正 TTL、missing key 加非法 TTL，并逐项断言失败后 value、TTL 和索引不变。
- [ ] 修改 `tests/command_test.cpp`，覆盖 `seconds * 1000` 可表示但 `system_clock::time_point` 不可表示的 `SET EX` 和 `EXPIRE`，并断言返回现有错误且数据不变。
- [ ] 修改 `tests/database_test.cpp`，覆盖 snapshot epoch milliseconds 的精确可表示边界和不可表示值，并断言非法加载不修改原 Database。
- [ ] Run `ctest --test-dir build --output-on-failure -R '^tinyredis-(database|command)-test$'`; expect both tests to pass.

## N4: Snapshot v1 reader 和 writer 对合法状态闭合
Dependencies: N3

- [ ] 修改 `src/database.h`，唯一定义 1 KiB key、1 MiB value 和 1,000,000 snapshot entries 的 inclusive limits（含边界限制）。
- [ ] 修改 `src/command.cpp`，删除重复的 key/value limits，并让 `SET`、`MSET` 及其他 key 检查复用 `src/database.h` 的 key/value constants。
- [ ] 修改 `src/database.cpp` 的 snapshot reader，使用 POSIX `open(O_RDONLY)` 区分 `ENOENT` 与其他打开错误，仅让 `ENOENT` 返回成功且保持 Database 不变。
- [ ] 修改 `src/database.cpp` 的 snapshot reader，使用 exact fd reads 解码现有 magic、native-endian 字段顺序和 `-1` no-TTL sentinel，拒绝字段截断、超限 count/key/value 和尾随字节。
- [ ] 修改 `src/database.cpp` 的 snapshot reader，用独立 `seen_keys` 集合拒绝所有重复 record，包括已过期 record，并拒绝除 `-1` 外的负 timestamp。
- [ ] 修改 `src/database.cpp` 的 snapshot reader，在 temporary map 和 expire index 中完成全部校验后再一次 move commit，并在 `std::bad_alloc` 时返回 false 且保持原 Database 不变。
- [ ] 修改 `src/database.cpp` 的 snapshot writer，在捕获一次 `now` 后只读扫描逻辑存活 entries，验证 count、key、value 和 epoch timestamp，再执行任何过期清理或文件操作。
- [ ] 修改 `src/database.cpp` 的 snapshot writer，仅在 preflight（预检）成功后按同一个 captured `now` 清除已过期 entries，并保持 snapshot v1 的 magic、字段顺序、native-endian 编码和 `-1` sentinel 不变。
- [ ] 在 `src/database.h` 和 `src/database.cpp` 的 `TINYREDIS_TESTING` 区域新增 one-shot（单次）`SnapshotFaultPoint::{LoadOpen, LoadAllocation}`，分别确定性产生 `EACCES` 和 decode allocation failure，并在触发后自动复位。
- [ ] 修改 `tests/database_test.cpp`，用固定 v1 bytes 覆盖非空 Database 上的 ENOENT no-op、ENOTDIR、`LoadOpen`、每个字段截断、边界与超限长度、重复 key、非法负 timestamp、尾随字节和 allocation failure。
- [ ] 修改 `tests/database_test.cpp`，覆盖正常 value/TTL round trip，并断言加载时仍未过期的 value、TTL 和 `ttlSize()` 被恢复。
- [ ] 修改 `tests/database_test.cpp`，覆盖超限 key、value 和 entry count 的 save preflight，断言返回 false、逻辑存活状态不变、过期物理 entry 未被清理且旧目标文件字节不变。
- [ ] 修改 `tests/command_test.cpp` 的 SAVE failure case，使用超限 Database state 触发 preflight failure，禁止再以 `/tmp` 作为目标路径。
- [ ] 修改 `tests/server_test.cpp`，覆盖启动时 snapshot 路径为 `ENOTDIR` 的情况，并断言 Server 在监听前返回 1。
- [ ] Run `ctest --test-dir build --output-on-failure -R '^tinyredis-(database|command|server)-test$'`; expect all snapshot reader, writer, SAVE, and startup tests to pass.

## N5: Snapshot 保存使用唯一临时文件原子替换目标
Dependencies: N4

- [ ] 修改 `src/database.cpp` 的 `Database::saveSnapshot()`，在目标同目录使用 `${path}.tmp.XXXXXX` 和 `mkstemp()` 排他创建 mode 0600 的本次专属临时文件。
- [ ] 修改 `src/database.cpp` 的 `Database::saveSnapshot()`，在写入完成后按当前 process umask 将临时文件调整为 `0644 & ~umask`，再执行 file `fsync()` 和真实 `close()`。
- [ ] 修改 `src/database.cpp` 的 `Database::saveSnapshot()`，固定执行 `write -> fchmod -> file fsync -> close -> rename -> parent directory open -> parent fsync -> parent close`，并让任一步失败都返回 false。
- [ ] 修改 `src/database.cpp` 的 `Database::saveSnapshot()`，使 rename 前失败只 unlink 本次专属临时文件并保留旧目标，rename 后失败保留完整新目标且不尝试反向 rename。
- [ ] 扩展 `src/database.h` 和 `src/database.cpp` 的 one-shot `SnapshotFaultPoint`，加入 `TempCreate`、`Write`、`FileChmod`、`FileFsync`、`FileClose`、`Rename`、`DirectoryOpen`、`DirectoryFsync` 和 `DirectoryClose`，并让 `FileClose` fault 在真实 close 后报告失败。
- [ ] 修改 `tests/database_test.cpp`，逐一触发 `TempCreate`、`Write`、`FileChmod`、`FileFsync`、`FileClose` 和 `Rename`，断言 fault 自动复位、旧目标仍可加载、live state 不变且不存在本次临时文件。
- [ ] 修改 `tests/database_test.cpp`，逐一触发 `DirectoryOpen`、`DirectoryFsync` 和 `DirectoryClose`，断言返回 false 且目标是可完整加载的新 snapshot。
- [ ] 修改 `tests/database_test.cpp`，预置 `${path}.tmp` 普通文件和指向 sentinel 的 symlink，断言两者在成功与失败保存后都未被读取、截断、rename 或删除。
- [ ] 修改 `tests/database_test.cpp`，在受控 umask 下保存 snapshot，并断言最终目标 mode 等于 `0644 & ~umask`。
- [ ] Run `ctest --test-dir build --output-on-failure -R '^tinyredis-(database|command)-test$'`; expect every snapshot fault point, cleanup rule, permission assertion, and successful round trip to pass.

## N6: EventLoop 在 epoll 和 kqueue 上提供同一注册与等待契约
Dependencies: N1

- [ ] 修改 `src/event_loop.h`，定义 `WaitStatus::{Ready, Timeout, Interrupted, Error}` 和 `WaitResult{status, events}`，并让 `EventLoop::wait()` 返回 `WaitResult`。
- [ ] 修改 `src/event_loop_backend.h`、`src/event_loop_epoll.cpp` 和 `src/event_loop_kqueue.cpp`，将 wait timeout、`EINTR`、ready events 与其他 syscall error 分别映射为四种 `WaitStatus`，且非 `Ready` 结果不携带 events。
- [ ] 修改 `src/event_loop.h` 和 `src/event_loop.cpp`，由 facade（门面）维护 registered FD set，使重复 `addRead()` 返回 false、未注册 `setWrite()` 返回 false、`remove()` 幂等。
- [ ] 修改 `src/event_loop.cpp` 的 `EventLoop::wait()`，过滤已移除 FD，并按 FD 对同一 batch 的 readable、writable 和 closed flags 做 OR merge，使每个 FD 最多返回一个 `Event`。
- [ ] 修改 `src/event_loop_epoll.cpp` 和 `src/event_loop_kqueue.cpp`，保持 level-triggered（水平触发）backend 操作，并把平台特有的重复注册语义隔离在 facade 之后。
- [ ] 修改 `src/server.cpp` 的 `Server::run()`，让 `Ready` 处理 events、`Timeout` 和 `Interrupted` 继续 tick、`Error` 停止 event loop 并在现有 cleanup/save 路径后返回 1。
- [ ] 修改 `CMakeLists.txt`，仅为 `tinyredis-event-loop-test` 和 `tinyredis-server-test` 的 EventLoop 源码定义 `TINYREDIS_TESTING=1`。
- [ ] 在 `src/event_loop.h` 和 `src/event_loop.cpp` 的 `TINYREDIS_TESTING` 区域新增 one-shot `EventLoopFaultPoint::{SetWriteEnable, SetWriteDisable, WaitInterrupted, WaitError}`，并在触发后自动复位。
- [ ] 修改 `tests/event_loop_test.cpp`，无平台条件地覆盖 duplicate add、unregistered write toggle、idempotent remove、write disable、同 FD 同时 readable/writable、peer close、timeout、interrupted wait、fatal wait 和 fault 自动复位。
- [ ] 修改 `tests/server_test.cpp`，用 pre-armed `WaitError` 启动无需 readiness handshake 的 child，断言 child 在 2 秒内保存 snapshot 并以状态 1 退出。
- [ ] Run `ctest --test-dir build --output-on-failure -R '^tinyredis-(event-loop|server)-test$'`; expect the platform backend tests and Server fatal-wait cleanup test to pass.

## N7: Command 回复在调用方预算内原子提交
Dependencies: N1

- [ ] 修改 `src/command.h`，定义 `CommandResult::{Complete, OutputTooLarge}`，并给 `appendExecuteCommand()` 增加 `size_t max_response_bytes` 参数和 `CommandResult` 返回值。
- [ ] 修改 `src/command.cpp` 的每个 command branch，使其先执行现有命令语义并生成受预算约束的 command-local response，仅在完整编码长度不超过 `max_response_bytes` 时一次 append 到 caller output；`MGET` 使用后续 preflight 步骤。
- [ ] 修改 `src/command.cpp` 的 `MGET` 分支，先收集每个 key 的 optional value view，并用 checked addition 计算 array header、bulk headers、payload 和 CRLF 的总编码长度。
- [ ] 修改 `src/command.cpp` 的 `MGET` 分支，在总编码长度超出预算时返回 `OutputTooLarge`，且不分配或构造聚合 RESP string。
- [ ] 修改 `src/server.cpp` 的 `appendResponse()` command 调用点，传入 `kMaxOutputBufferBytes - connection.output.size()`，并在 `OutputTooLarge` 时保持现有 silent-close（静默关闭）策略，供 N8 后续改为 pending-byte 计费。
- [ ] 修改 `tests/command_test.cpp` 的 `runCommand()` wrapper 和全部直接调用者，使默认预算足以容纳现有期望回复。
- [ ] 修改 `tests/command_test.cpp`，分别用比实际回复少 1 byte 和恰好相等的预算覆盖 PING、PING message、GET、MGET 和固定错误回复，并断言超限时 sentinel output 原样不变。
- [ ] 修改 `tests/command_test.cpp`，用 5 个 1 MiB values 覆盖 MGET checked preflight，并断言返回 `OutputTooLarge` 且 output 不变。
- [ ] 修改 `tests/command_test.cpp`，让 SET 的成功回复超预算，并断言返回 `OutputTooLarge` 但已写入的 value 不回滚。
- [ ] Run `ctest --test-dir build --output-on-failure -R '^tinyredis-(command|server)-test$'`; expect all command budget tests and existing Server behavior tests to pass.

## N8: 每个连接的待发送输出始终不超过 4 MiB
Dependencies: N6, N7

- [ ] 修改 `src/server.cpp` 的 `Connection`，新增以 `output.size() - outputOffset` 计算 pending bytes 的方法，并在追加新回复前 compact 已发送前缀。
- [ ] 修改 `src/server.cpp` 的 command 执行路径，按 `4 MiB - pending bytes` 向 `appendExecuteCommand()` 传递预算，使已发送前缀不再占用预算。
- [ ] 修改 `src/server.cpp` 的 request-too-large 和 invalid-protocol 回复路径，使用同一个 bounded raw-response helper，使 Server 自身回复只能完整 append 或零 append。
- [ ] 修改 `src/server.cpp` 的 output-too-large 路径，丢弃该连接的 pending output 并立即关闭该连接，不回滚已执行的命令且不影响其他连接。
- [ ] 修改 `src/server.cpp` 的 read path，在 `EventLoop::setWrite(fd, true)` 返回 false 时立即 remove、close 并释放该 connection slot。
- [ ] 修改 `src/server.cpp` 的 write-complete path，在 `EventLoop::setWrite(fd, false)` 返回 false 时立即 remove、close 并释放该 connection slot。
- [ ] 在 `src/server.h` 和 `src/server.cpp` 的 `TINYREDIS_TESTING` 区域新增 one-shot `ServerFaultPoint::PartialSendThenWouldBlock`，使下一次 send 真实发送指定前缀、紧接的一次 send 返回 `EAGAIN`，随后自动复位。
- [ ] 修改 `tests/server_test.cpp`，预加载 1 MiB value 后以 3 个 GET 产生合法输出，通过 `PartialSendThenWouldBlock` 留下已发送前缀，再发送第 4 个 GET，并断言连接收到完整有序的 4 个回复且未因完整 `output.size()` 被误关闭。
- [ ] 修改 `tests/server_test.cpp`，让单连接的 4 个最大 GET 超出 4 MiB，断言该连接关闭而第二个连接仍能收到 PONG。
- [ ] 修改 `tests/server_test.cpp`，分别触发 `SetWriteEnable` 和 `SetWriteDisable`，断言故障连接关闭、connection slot 可复用且另一连接仍能收到 PONG。
- [ ] Run `ctest --test-dir build --output-on-failure -R '^tinyredis-(command|server|event-loop)-test$'`; expect output-budget, partial-send, write-interest, pipeline, half-close, and connection-limit tests to pass.

## N9: SIGTERM 和 SIGINT 共用确定性保存退出流程
Dependencies: N5, N6, N8

- [ ] 修改 `src/server.cpp` 的 signal state，使 shutdown requested flag、signal-visible pipe FD 和 test fault flag 都使用 `volatile sig_atomic_t`。
- [ ] 修改 `src/server.cpp` 的 signal setup，在临时阻塞 SIGTERM 与 SIGINT 时保存并安装两个 handler，并在任一安装失败时恢复已修改的 handler 后返回 1。
- [ ] 修改 `src/server.cpp` 的 `handleShutdownSignal()`，先设置 shutdown flag，再 best-effort write self-pipe，并保存和恢复 `errno`。
- [ ] 修改 `src/server.cpp` 的 `Server::run()` event loop，在每次 wait 返回后、处理 listener 或 client event 前检查 shutdown flag，使同一 batch 不再 accept 或执行 command。
- [ ] 修改 `src/server.cpp` 的 shutdown cleanup，停止 listener、调用一次 `Database::saveSnapshot()`、立即关闭所有 clients、移除 signal pipe、恢复 SIGTERM/SIGINT handlers，并按 save 结果返回 0 或 1。
- [ ] 在 `src/server.h` 和 `src/server.cpp` 的 `TINYREDIS_TESTING` 区域新增 one-shot `failNextShutdownPipeWriteForTest()`，使 handler 跳过一次 pipe write 但仍设置 shutdown flag。
- [ ] 修改 `tests/server_test.cpp`，将 graceful shutdown case 参数化为 SIGTERM 和 SIGINT，并断言 child 在 2 秒内退出、状态为 0、snapshot 可加载且已确认写入的数据存在。
- [ ] 修改 `tests/server_test.cpp`，触发 shutdown-pipe write fault 后发送 SIGTERM，断言 child 不依赖 pipe readability 并在一个 100 ms tick 加调度余量内进入相同保存退出路径。
- [ ] 修改 `tests/server_test.cpp`，覆盖 save failure、pending output、连续 SIGTERM/SIGINT 和两个 previous handler 的恢复，分别断言退出状态、最终连接关闭、无重入挂死和 handler 值恢复。
- [ ] Run `ctest --test-dir build --output-on-failure -R '^tinyredis-server-test$'`; expect all signal cases to finish within 2 seconds and restore valid snapshots or return the asserted save-failure status.

## N10: CLI 对端口、SIGPIPE 和 RESP 长度输入可控失败
Dependencies: N1

- [ ] 修改 `src/cli.cpp` 的 `parseArgs()`，在完整 `std::stoi()` 解析后仅接受 `1..65535`，并保持现有 `+1`、前导空白和前导零词法行为不变。
- [ ] 修改 `src/cli.cpp` 的 `connectServer()`，在 macOS 上于 `connect()` 前成功设置 `SO_NOSIGPIPE`，设置失败时关闭 socket 并返回失败。
- [ ] 修改 `src/cli.cpp` 的 `sendAll()`，在支持的平台使用 `send(..., MSG_NOSIGNAL)`，将 zero return 视为失败，并保持 write error 返回 false 的现有调用链。
- [ ] 修改 `src/cli.cpp` 的 `readLine()`，在读取超过 `kMaxRespRequestBytes` 且尚未遇到 line ending 时返回 false。
- [ ] 修改 `src/cli.cpp` 的 bulk/array length parser，用 `std::from_chars()` 完整解析并拒绝空值、尾随字符、overflow 和小于 `-1` 的值。
- [ ] 修改 `src/cli.cpp` 的 `printResponseValue()`，仅在 bulk length 不超过 `kMaxRespBulkLength`、array length 不超过 `kMaxRespArrayLength` 后读取或分配 payload。
- [ ] 修改 `CMakeLists.txt`，仅为 `tinyredis-cli-test` 定义 `TINYREDIS_TESTING=1`。
- [ ] 在 `src/cli.h` 和 `src/cli.cpp` 的 `TINYREDIS_TESTING` 区域暴露 `configureNoSigPipeForTest()` 和 `sendAllForTest()` wrapper，使 closed-peer write 可在独立 child 中确定性执行。
- [ ] 修改 `tests/cli_test.cpp` 的 response fixture，使用带 2 秒 deadline 的 child writer 和 pipe/socketpair，使超过 pipe capacity 的 line 与 fragmented response 测试不会在 parser 启动前阻塞。
- [ ] 修改 `tests/cli_test.cpp`，覆盖端口 0、-1、1、65535、65536，并断言前三个非法值不写入 `CliOptions::port`、两个合法边界被接受。
- [ ] 修改 `tests/cli_test.cpp`，在恢复默认 SIGPIPE disposition 的 child 中先调用 `configureNoSigPipeForTest()`，再关闭 peer 并调用 `sendAllForTest()`，断言 child 正常返回状态 1 而不是被 SIGPIPE 终止。
- [ ] 修改 `tests/cli_test.cpp`，覆盖 fragmented response、peer EOF、超长 line、invalid/partial/overflow/oversize bulk length 和 array length，并断言 `printResponse()` 返回 false、无 exception 且测试在 deadline 内结束。
- [ ] Run `ctest --test-dir build --output-on-failure -R '^tinyredis-cli-test$'`; expect all CLI boundary and failure-path tests to pass.

## N11: Benchmark 报告保留调用参数并清理本次资源
Dependencies: N1

- [ ] 修改 `benchmark.sh`，从 `${BASH_SOURCE[0]}` 定位仓库根目录，使默认 `TINYREDIS_SERVER_BIN` 指向仓库的 `build/tinyredis-server`，并保持用户提供的相对 override 与默认 output 相对于调用 cwd。
- [ ] 修改 `benchmark.sh` 的 `write_report_header()`，通过 `git -C "$repo_root"` 读取 revision 和 dirty state。
- [ ] 修改 `benchmark.sh` 的 custom profile 路径，用 Bash 3.2-compatible array 保存原始 `"$@"`，并让 `profile_requests()`、`profile_data_size()`、preload 和 `redis-benchmark` 调用始终使用 `"${current_profile_args[@]}"`。
- [ ] 修改 `benchmark.sh` 的 default profile 路径，将每一受控 profile line 解析到同一个参数 array，再复用 custom profile 的执行路径。
- [ ] 修改 `benchmark.sh` 的报告输出，用逐参数 shell quoting 记录 profile，使空参数、空格和 wildcard 的原始 argument boundaries（参数边界）可重建。
- [ ] 修改 `benchmark.sh` 的 `start_case_server()` 和 `cleanup_current_server()`，为每次 invocation 创建并删除唯一 server log，启动失败时先将该 log 输出到 stderr。
- [ ] 修改 `benchmark.sh` 的 `stop_server()`，在 SIGTERM 后使用 bounded wait，并在 deadline 后 SIGKILL 和 reap 本次 child，避免 cleanup 无限等待。
- [ ] 修改 `benchmark.sh` 的 SAVE metadata，使 `save_note` 明确报告 `SAVE_BENCHMARK_REQUESTS` 次请求、1 client 和 pipeline 1，并保留现有 `save_requests` 字段。
- [ ] Run `bash -n benchmark.sh && ./benchmark.sh --help`; expect syntax validation and help output to succeed from the repository cwd.
- [ ] Run `repo=$PWD; (cd /tmp && "$repo/benchmark.sh" --help)`; expect help output to succeed without resolving files relative to `/tmp`.
- [ ] Run `tmpdir=$(mktemp -d); TINYREDIS_OUTPUT="$tmpdir/report" TINYREDIS_PORT=16379 SAVE_DATASET_REQUESTS=10 SAVE_BENCHMARK_REQUESTS=2 ./benchmark.sh -n 10 -c 1 -P 1`; expect the report to preserve the custom argv, report 2 SAVE requests, and leave no benchmark server, dump, or server log after normal completion.

## N12: 文档与最终验证准确描述已交付边界
Dependencies: N2, N5, N8, N9, N10, N11

- [ ] 修改 `README.md`，将完整 Redis-compatible 声明替换为有限 RESP2/Redis command compatibility，并列出 PING、SET、GET、EXISTS、DEL、INCR、DECR、MGET、MSET、EXPIRE、TTL、PERSIST 和 SAVE。
- [ ] 修改 `README.md`，记录 C++17、macOS/kqueue、Linux/epoll、`./build.sh [debug|release]`、`./test.sh`、server 启动和 CLI one-shot/interactive 命令。
- [ ] 修改 `README.md`，记录 server `--port`、`--dump-file`、CLI `-p`、默认端口 6379、CLI 固定连接 `127.0.0.1`，且不声明不存在的 help 或 host 参数。
- [ ] 修改 `README.md`，记录 128 connections、4 MiB pending output、RESP array/request/bulk、1 KiB key 和 1 MiB value limits。
- [ ] 修改 `README.md`，记录 snapshot 是 native-endian `TINYREDIS-SNAPSHOT-v1` 而非 Redis RDB，并记录 SIGTERM/SIGINT 都保存后退出。
- [ ] 修改 `README.md`，明确 server 默认监听所有 IPv4 interfaces 且没有认证，不应暴露到不可信网络。
- [ ] 修改 `README.md`，将 benchmark 描述为依赖外部 `redis-benchmark` 的手工单次回归工具，并明确结果不是统计结论、CI gate 或性能承诺。
- [ ] 修改 `.gitignore`，仅新增根目录 `/dump.rdb` 规则。
- [ ] Run `bash -n build.sh test.sh benchmark.sh`; expect all maintained shell entry points to pass syntax validation.
- [ ] Run `./test.sh && ./build.sh release`; expect all 6 tests and the production Release build to pass on the current platform.
- [ ] Run `git check-ignore -q dump.rdb`; expect the root default snapshot path to be ignored.
- [ ] Run `git diff --check`; expect no whitespace errors.
