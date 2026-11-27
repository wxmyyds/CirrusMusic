# Windows 原生音频测试端点

Windows 测试 job 在依赖准备和编译之前运行 [`prepare-windows-audio.ps1`](prepare-windows-audio.ps1)，只在临时 Actions runner 安装固定版本的系统虚拟输出端点。生产构建和产品安装器不调用此脚本，驱动不进入产品包或 Release artifact。

## 固定来源与信任边界

- 官方版本：**VB-CABLE Pack46**。
- 下载地址：`https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack46.zip`。
- 锁定 SHA-256：`bc44e8974191a5a2427d6c979e2dab08dbb09925f3f435dfa2f0ba3e45b6eb90`；2026-11-27 实测大小 `1324680` bytes。
- 包含 AMD64 与 ARM64 SYS；INF `vbMmeCable64_win10.inf` 的驱动版本为 `10/31/2026,3.5.2.0`，硬件 ID 为 `VBAudioVACWDM`。
- 官方 setup 为 AMD64 `VBCABLE_Setup_x64.exe`；包没有独立 ARM64 setup EXE。ARM64 runner 依赖 Windows 的 setup 执行支持，但驱动、PowerShell、MSVC、Qt 和测试进程均要求原生 ARM64。

本次下载的静态清单（hash 是实测值，不是厂商提供的签名替代物）：

| 包内文件 | SHA-256 | 架构/用途 |
| --- | --- | --- |
| `vbMmeCable64_win10.inf` | `cecf9181b6c85842d707330efc594824a749a1d9e01db46eba0e60e72f0a66ee` | AMD64 / ARM64 安装节 |
| `vbaudio_cable64_win10.cat` | `5d24c17858b49ad83bb856c66c24c5c2aecdb89cbaae2f8179c15d351124fa9f` | INF 指定的 catalog |
| `vbaudio_cable64_win10.sys` | `7ebcbbe7004d185f7b29613cd27fdc2dd71c1712224b767a74d2f66bdc33a7ef` | AMD64 |
| `vbaudio_cable64arm_win10.sys` | `85c217af826c2e1d478b52afc7c2248889c8478030d44e8134bd4c23b8915216` | ARM64 |
| `VBCABLE_Setup_x64.exe` | `734c35dfa6d98f48782a451633ceb471166ec70d60482fd89a1123d0ee3c4f41` | AMD64 setup |
| `readme.txt` | `f865f3e78e37006d48e56c93f51eff4ca79acda9969854400d79bfa3db38a8d5` | 安装说明 |

脚本在解压、执行包内程序前校验整个 ZIP；内容变化即失败。Windows 上还要求 `Get-AuthenticodeSignature` 返回 `Valid` 且 publisher 精确匹配：

| 文件 | publisher |
| --- | --- |
| `VBCABLE_Setup_x64.exe` | `BUREL VINCENT Entrepreneur individuel` |
| `vbaudio_cable64_win10.cat` | `Microsoft Windows Hardware Compatibility Publisher` |

f6c8827 的 ARM64 与 x64 runner evidence 均记录 setup 签名 `Valid`、publisher 与预期一致；ARM64 的 Windows PnP 日志进一步记录 catalog WHQL 评分并成功加载驱动。脚本不改变签名策略、信任根或驱动强制签名设置；静态解析证书名称不能替代 Windows 信任链验证。

## 唯一安装与验收链路

1. 仅允许管理员权限的 Windows Actions runner，并核对 OS 与 PowerShell 原生架构。
2. 下载固定 ZIP、校验哈希及可信签名后运行官方 setup `-i -h`。明确的非零退出码或 `3010` / `1641` 重启状态失败，不重试安装。
3. 安装器两分钟内正常退出时，继续核对退出码。若它仍等待，脚本不会仅凭超时判成功：只有同一 Pack46 驱动的 PnP 状态、签名、内核服务和活动默认端点都通过，才终止仍等待的 setup 进程，并记录超时/终止状态；任何检查未通过仍失败。
4. 依次刷新 `AudioEndpointBuilder`、`Audiosrv` 并确认 Running；不重启 runner、不修改 registry、不另行选择端点。
5. 最多等待 60 秒，要求至少一个状态为 `OK` 的 render `AudioEndpoint`，且其 `DEVPKEY_Device_Parent` 链在限定深度内到达上述已签名 Pack46 PnP 设备；不依赖 FriendlyName，因为 Windows Server runner 可能显示 `Speakers (VB-Audio Virtual Cable)`，ARM64 显示 `CABLE Input (VB-Audio Virtual Cable)`。通过 `IMMDeviceEnumerator` 确认当前 `eRender / eMultimedia` 默认端点的 MMDevice ID 恰好对应这些已验证 render endpoint 之一；默认端点未通过该身份关系检查时失败。
6. 将实际 MMDevice ID 经 `GITHUB_ENV` 提供给 AudioEngine 测试。Windows Actions 每例都要求 Qt `QMediaDevices::defaultAudioOutput()` 存在、默认标志为真且 ID 匹配。
7. 现有 AudioEngine 用例沿生产路径打开默认 `QAudioSink`，验证播放、结束、seek、暂停和衔接。音量为零策略与原有断言保留；这不是物理扬声器可听性测试。设备缺失直接失败，不使用 `QSKIP`、fake sink 或弱化断言。

设备枚举只证明 Qt 指向预期端点；真实打开/消费须由 AudioEngine 用例证明。`ready-for-qt-tests` 只表示脚本的系统检查通过，不等于 Qt 播放验收通过。

## Actions 证据与当前边界

### 上游同步基线的缺设备证据

历史 run [45935196737](https://github.com/wxmyyds/CirrusMusic/actions/runs/45935196737) 的实际 SHA 为 `a76fa303bd8122659c35d22106f973ac633c8607`，与 `d4defd0b8020ff9bab35d53fcb9e5ceda0bac3d9` 同 tree，但不是相同 commit。该 run 两个 Windows 架构的 AudioEngineTest 均为 2 passed、0 failed、17 skipped，skip 原因为 `no audio output device`；这是旧 tree 的缺设备证据，不是端点修复验收。

### 固定 Pack46 的实际测试

端点实现 SHA **`f6c882793879bf8d6b9c72a46c161ac146d44ab7`**，run [45980084660](https://github.com/wxmyyds/CirrusMusic/actions/runs/45980084660)：

- **Windows ARM64 job `140441577789`**：`prepare-windows-audio.ps1` 记录 `ready-for-qt-tests`；系统默认端点、签名驱动、问题码和运行服务检查均通过。AudioEngine CTest 通过，CI 日志记录这组真实默认端点用例完成。整份 job **仍失败**：`tst_backend_runtime.exe` 的 QtTest 用例报告 55 passed、0 failed，进程退出时发生 `0xc0000005`，附近符号为 `QtMultimediaPrivate::refreshWarmupClient`；CTest 因 `backend_runtime (SEGFAULT)` 失败。不能把该架构整个 job 称为通过。
- **Windows x64 job `140441577860`**：setup 两分钟未退出，脚本在进入独立端点验收前失败。上传的 `setupapi.dev.log` 显示签名 catalog 通过 WHQL 验证、PnP 驱动安装和设备启动成功；但该版本脚本未记录后续端点默认状态，且没有 x64 AudioEngine 运行结果。不能从安装日志推断 Qt 播放成功。
- run [46007066286](https://github.com/wxmyyds/CirrusMusic/actions/runs/46007066286)，SHA `1c2e8a462077947d8c55f02631ec37e83f72c3b5`，实际执行了安装超时后的独立检查；Windows Server x64 暴露默认 render endpoint 名为 `Speakers (VB-Audio Virtual Cable)`，旧 name-based matcher 因期待 `CABLE Input ...` 而失败。该 run 因而没有 x64 Qt AudioEngine 结果。当前未提交的 matcher 改为沿 render endpoint 的 PnP parent chain 匹配已验证驱动 instance ID，再核默认 MMDevice ID；此身份关联改动尚未在 Actions 运行。

## 厂商重启建议

官方下载页、Pack46 `readme.txt` 与[官方手册](https://vb-audio.com/Cable/VBCABLE_ReferenceManual.pdf)建议安装后重启；README 原文为 `To finalize installation, you must reboot your computer.`。f6c8827 的 ARM64 runner 在未重启的 job 中通过实际 Qt AudioEngine 用例，说明该 runner 会话当时能够消费该端点，但不能据此宣称所有 Windows 版本/机器均免重启。脚本不重启 runner；若 setup 明确报告重启状态，或设备未在当前会话达到前述运行状态，必须失败。

第三方 Chocolatey 社区包的免重启报告不能代替当前版本证据；本 CI 不调用旧 Pack43/旧 INF 配方，也不以暂存 INF、强行指定默认设备、重试安装或跳过用例绕过限制。

## 失败诊断与验证范围

脚本尽可能在 `${RUNNER_TEMP}/cirrus-windows-audio-evidence/` 写出：

- `endpoint.json`：实际 checkout 完整 SHA、OS/进程架构、包哈希、签名状态、publisher、证书 thumbprint、安装退出码、安装器超时/终止状态、服务、默认端点及其 PnP parent chain、驱动和错误信息。
- `installer-stdout.txt`、`installer-stderr.txt` 与 Windows `setupapi.dev.log`。

这些文件与测试报告随 `test-results-Windows-x86_64` / `test-results-Windows-arm64` artifact 保留 3 天；仅供测试审计，不影响产品包清单。不能以本机静态分析、签名元数据、旧 SHA 或 agent 推论代替新 SHA 的 Windows 原生 job 和实际 QtTest 报告。SHA `1c2e8a462077947d8c55f02631ec37e83f72c3b5` 已运行安装超时后的检查，但 x64 在旧 FriendlyName 条件处失败；当前基于 PnP parent identity 的修正尚未经过 Actions。不能宣称端点工作完成。

本地不安装驱动、不配置/编译/运行项目测试。PowerShell/PnP/COM/Qt 行为只以原生 Windows Actions 的实际证据为准。

## 历史包和签名调查

2026-11-27 下载的 Pack46 原始 INF/catalog/SYS/setup 静态 hash 如上；INF 中 catalog 为 `vbaudio_cable64_win10.cat`，包含 AMD64、ARM64 安装节。静态解析 PE/PKCS#7 只曾用于确定待校验 publisher；最终受信任状态以 Windows `Get-AuthenticodeSignature` 和 PnP catalog 校验日志为准。官方旧地址 Pack45 曾实测 SHA-256 `b950e39f01af1d04ea623c8f6d8eb9b6ea5c477c637295fabf20631c85116bfb`，未接入 CI。

Windows Server 2022 x64 的支持范围与 Windows 11 ARM64 不应相互推断；未来若 runner image、Pack46 字节或 setup 行为变化，必须按真实目标重新验证。

**证据边界：**上述具体运行结果仅绑定标出的完整源码 SHA 与 run/job。timeout follow-up 已在 `1c2e8a4...` 实际执行，但 x64 端点识别失败；同一 run 的 ARM64 端点准备通过后在编译阶段失败，未运行 QtTest。更改后的 PnP parent identity 匹配尚未由 Actions 验证；此前 `f6c8827...` 的 ARM64 AudioEngine 用例通过，但该 job 因 backend_runtime 崩溃失败。本文不代表整体修复验收完成。
