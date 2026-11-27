# Windows 原生音频测试端点：阻塞记录

调查日期：2026-11-27。基线：`fix/agents-audit-remediation` /
`d4defd0b8020ff9bab35d53fcb9e5ceda0bac3d9`。

## 结论与本次改动

**当前不能把官方 VB-CABLE 接入视为已经可用的 Windows CI 修复。**
官网下载包具有 AMD64、ARM64 驱动和签名结构，但官网、包内 README 和官方手册均要求安装后重启。
本次没有取得当前原生 Windows runner 上免重启安装、签名信任验证、端点枚举及真实 Qt 消费的证据。
按本任务“不得中断 runner 重启，无法证明则记录阻塞”的约束，停止安装链路实施。
这不是对所有可能的免重启安装方式作出技术上不可能的判断。

本次只将 `tests/unit/audio_engine_test.cpp` 中设备缺失的 `QSKIP` 改为失败断言；
保留 Linux 私有 PulseAudio 检查及所有既有用例断言。
这会暴露缺少输出设备的原生测试环境，不能使尚无端点的 Windows job 通过。
没有修改音频核心、`.github/workflows/ci.yml`、生产打包或安装器；
没有加入未验证的安装命令、PowerShell 脚本、fake Qt sink、重启绕过或新的 skip。
由于没有新增 PowerShell 脚本，不存在可报告的新脚本语法或 PSScriptAnalyzer 验证结果。

## 旧 CI 的实际证据

读取的 run：[45935196737](https://github.com/wxmyyds/CirrusMusic/actions/runs/45935196737)，
创建时间 `2026-11-27T03:36:54Z`。API 返回的实际 `headSha` 为
`a76fa303bd8122659c35d22106f973ac633c8607`，分支为 `sync/upstream-master-4378ed0`，
不是本次基线 SHA。不过两者的 Git tree 均为
`dc23d31118942510af6d5584bdab2f68e29ea0ea`，`git diff` 无差异。
因此只能称为基线同树的历史结果，不能当作本次新提交的验收。

| 架构 | Job ID | 日志中的 OS / image | Image version | AudioEngineTest 实际汇总 |
| --- | --- | --- | --- | --- |
| x86_64 | `140296826367` | Windows Server 2022 `10.0.20348` / `windows-2022` | `20261123.376.1` | 2 passed, 0 failed, **17 skipped** |
| arm64 | `140296826340` | Windows 11 Enterprise `10.0.26200` / `windows-11-vs2026-arm64` | `20261115.219.1` | 2 passed, 0 failed, **17 skipped** |

分别下载并读取了下列测试报告 artifact 中的 `Testing/Temporary/LastTest.log`；
每个报告均有 17 条 `AudioEngineTest` 的 `no audio output device` 跳过信息。
也读取了 `junit.xml`，没有以顶层 job success 代替用例结果。

| Artifact | ID | 下载外层 ZIP 的实测 SHA-256（与 API digest 一致） |
| --- | --- | --- |
| `test-results-Windows-x86_64` | `15171298778` | `83c6ca200270a08066c8b3563f966a3f8ab27cf74475a2b3ca832be892cf23ac` |
| `test-results-Windows-arm64` | `15172309220` | `1f4f238c327fdc9a2188a52e4addfec701b975abb759cb5caaaebbfd47dd565d` |

以上 image 信息来自该次 run 的日志，不保证未来重新调度时仍使用同一 image 版本。

## 官方候选包与本次实测清单

本次读取的[官方下载页](https://vb-audio.com/Cable/)推荐的是 **Pack46**，标注
`NOV 2026 / XP to WIN11 32/64 bits/Arm64`，并非旧的 Pack45。
固定版本下载地址：

`https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack46.zip`

2026-11-27 通过 HTTPS 实际下载，未执行其中任何程序：

- 文件大小：`1324680` bytes。
- ZIP SHA-256：`bc44e8974191a5a2427d6c979e2dab08dbb09925f3f435dfa2f0ba3e45b6eb90`。
- HTTP `Last-Modified`：`Sat, 31 Oct 2026 10:17:59 GMT`。
- `vbMmeCable64_win10.inf`：`DriverVer = 10/31/2026,3.5.2.0`。
- INF 使用 `CatalogFile=vbaudio_cable64_win10.cat`；同时包含
  `NTamd64.10.0...16299`、`NTARM64.10.0...16299` manufacturer/install sections。
- 硬件 ID 字符串为 `VBAudioVACWDM`。

| 包内文件 | 实测 SHA-256 | 静态架构/用途 |
| --- | --- | --- |
| `vbMmeCable64_win10.inf` | `cecf9181b6c85842d707330efc594824a749a1d9e01db46eba0e60e72f0a66ee` | AMD64 / ARM64 安装节 |
| `vbaudio_cable64_win10.cat` | `5d24c17858b49ad83bb856c66c24c5c2aecdb89cbaae2f8179c15d351124fa9f` | INF 指定的 catalog |
| `vbaudio_cable64_win10.sys` | `7ebcbbe7004d185f7b29613cd27fdc2dd71c1712224b767a74d2f66bdc33a7ef` | PE machine `0x8664`，AMD64 |
| `vbaudio_cable64arm_win10.sys` | `85c217af826c2e1d478b52afc7c2248889c8478030d44e8134bd4c23b8915216` | PE machine `0xaa64`，ARM64 |
| `VBCABLE_Setup_x64.exe` | `734c35dfa6d98f48782a451633ceb471166ec70d60482fd89a1123d0ee3c4f41` | PE machine `0x8664`，AMD64 安装器 |
| `readme.txt` | `f865f3e78e37006d48e56c93f51eff4ca79acda9969854400d79bfa3db38a8d5` | 含必须重启的安装说明 |

包中没有单独的原生 ARM64 setup EXE。驱动本身为原生 ARM64，不应把 AMD64 安装器的
存在误认为驱动架构不支持 ARM64，也不能据此声称安装器已经在 Windows 11 ARM64 上验证。
官方手册明确声明 Windows 10/11 ARM64 支持；该声明和静态架构检查都不等于当前 runner 安装成功。
x64 runner 实际是 Windows Server 2022；上述下载页/手册措辞没有单独明确确认这个 Server image 的支持。

为避免沿用旧版本假设，也实际下载检查了仍可访问的官方旧地址
`https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip`：
`1318877` bytes，SHA-256
`b950e39f01af1d04ea623c8f6d8eb9b6ea5c477c637295fabf20631c85116bfb`；
其 README 同样要求重启。该旧包不是本次官网推荐包，也没有被接入 CI。

这些哈希是本次下载的实测值，不是厂商发布的校验值，也不证明 URL 永久不可变。
未来实现必须在执行/安装前校验锁定的哈希，字节变化时失败，不能自动接受新哈希。

## 签名证据的边界

对 Pack46 的 setup 和两个目标 SYS，静态解析 PE certificate table 后使用
`openssl pkcs7 -inform DER -print_certs` 读取证书材料；catalog 同样可以读取 PKCS#7 证书。

- `VBCABLE_Setup_x64.exe` 和两个 SYS 的外层签名结构含证书 subject
  `BUREL VINCENT Entrepreneur individuel`，issuer 为
  `GlobalSign GCC R45 EV CodeSigning CA 2020`。
- catalog 的证书材料含 `Microsoft Windows Hardware Compatibility Publisher`，issuer 为
  `Microsoft Windows Third Party Component CA 2014`。

**这只是包内签名结构及证书元数据，不是 Windows Authenticode/kernel policy 验证成功。**
本机为 Linux，没有执行 `Get-AuthenticodeSignature`、`signtool verify /kp`、catalog 成员哈希验证、
Windows 信任链/时间戳验证或驱动加载。尝试用 OpenSSL CMS 直接验证 catalog 时因其封装格式报
ASN.1 tag 错误，未取得通过结果；不能把证书解析成功描述为签名验证通过。
这也不构成对包签名无效的判断。

## 明确的重启阻塞

三个官方材料一致：

1. 下载页安装说明要求安装后和卸载后重启。
2. Pack46 内 `readme.txt` 第 45 行：`To finalize installation, you must reboot your computer.`
3. [官方 Reference Manual](https://vb-audio.com/Cable/VBCABLE_ReferenceManual.pdf)，
   October 2024 / revision 3，第 4 页安装步骤 3 同样要求重启。
   本次下载手册的 SHA-256 为
   `e1574293a9b57cea7e5a0299ff809d7a819aad91ebac737e5209e596fcb700e0`。

没有获得厂商对当前两个 runner image 的免重启安装保证，也没有取得相应新 SHA Actions 实测。
因此不能以启动 `AudioEndpointBuilder`/`Audiosrv`、忽略重启返回码、仅将 INF 加入 Driver Store、
延时等待、重试安装或强制声明默认设备来代替此条件。

## 后续可以解除阻塞的验收证据

只有在确认符合免重启约束的安装路径后，才应加入实际 CI 配置：

- 只在 `ci.yml` 的 Windows 测试 job 安装，生产依赖、包和安装器不引用驱动。
- 下载前锁定版本 URL；运行任何包内程序前校验 SHA-256，检查系统/INF/SYS 架构，
  验证 Authenticode、kernel-policy 签名及 catalog 对目标 INF/SYS 的成员覆盖。
- 在原生 AMD64 和 ARM64 runner 上取得实际安装结果；遇到需要重启的状态立即失败，
  不重启、中断或以重连 runner 的方式绕过。
- 确认 `AudioEndpointBuilder` 和 `Audiosrv` 为 Running；取得目标 render endpoint 的身份及 Active 状态，
  将其设置为默认播放端，并由真实 `QMediaDevices` 枚举和核对默认端点。
- 运行原有 `AudioEngineTest`，证明生产 Qt backend 真正打开并消费系统端点，保留全部既有断言；
  不能仅以设备管理器、registry、服务启动、枚举列表或安装器退出码作为验收。
- 对新增 PowerShell 脚本进行 parser/PSScriptAnalyzer 静态检查，并读取两个原生 Windows job 的实际测试报告。

本次没有本地配置、构建、安装或运行项目测试；没有启动任何新 Actions run。
最终环境可用性、Qt 打开/消费及测试结论只能来自后续完整新源码 SHA 的 Actions，
不能复用上述同树旧 run、Linux 静态检查或签名元数据作为成功证据。
