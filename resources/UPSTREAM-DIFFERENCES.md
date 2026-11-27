# 与上游的详细差异

本文只记录当前 fork 相对于上游 [`Guang233/CirrusMusic`](https://github.com/Guang233/CirrusMusic) 的产品、代码、依赖和交付差异。上游本身已有的通用功能不在本文重复说明；构建与发布操作细节见 [`packaging/README.md`](../packaging/README.md)。

## 差异概览

- 增加 Bilibili 辅助音源、Bilibili 账号凭据和按实际播放源选择歌词的链路。
- 增加播放源身份、歌词会话和缓存安全约束，避免不同音源、账号或旧请求之间互相污染。
- 移除云盘上传/管理链路；移除 MiSans 正文字体，保留 Material Symbols 图标字体并支持手动字体选择。
- 加强跨平台私有目录、缓存和文件访问检查，并调整 Linux 播放状态/歌词的只读导出行为。
- 将 Linux、Windows、macOS 的构建和发布整理为原生平台工作流；Linux 使用发行版依赖，提供发行版安装包，库源码归档单独交付。

## 1. Bilibili 辅助音源、账号和播放源路由

上游的网易云播放链路仍是首选。fork 在它外层增加一个统一的音源路由器：

- 设置中的 Bilibili 音源开关默认关闭。启用后，先照常向网易云请求音源；仅在网易云明确返回权限受限，或成功返回的资源仅为试听片段时，才允许把该歌曲交给 Bilibili。
- 网易云网络错误、超时、限流、解析或解码失败等普通错误不会触发换源；不会用换源掩盖普通请求失败。
- 网易云响应中明确表示无权限的情况会映射为专门的权限失败，例如受限的 `404`、空地址同时带有正数 `fee` 或 `cannotListenReason == 1`。不能把所有播放失败都理解为可以切换到 Bilibili。
- 选定 Bilibili 后，它就是该曲目本次播放的音源。Bilibili 匹配或获取流失败会作为该曲目的失败处理，不再偷偷回退到网易云试听片段。
- Bilibili 搜索匹配使用歌曲名称、首位歌手和时长等信息，按标题、作者、时长进行评分并选择视频及具体分 P。候选时长过度偏离原曲时会被过滤；这是启发式匹配，不保证得到原版录音。
- 当前播放快照记录实际 provider、播放代次以及 Bilibili 的 `bvid`、`cid`、流标识。队列、歌词和桌面状态不会只根据网易云歌曲 ID 假定实际内容来自网易云。
- Bilibili Cookie 可以在账户设置中粘贴、校验和清除，并通过系统钥匙串保存；不会因为密钥环不可用而降级写入普通设置文件。

主要实现：`src/application/playback/provider_router.*`、`src/infrastructure/bilibili/`、`src/infrastructure/netease/music/stream_resolver.*`、`src/domain/playback/media_source.h`、`ui/presentation/backend/backend_playback.*`、`ui/app/settings/AccountSettings.qml` 和 `ui/app/settings/PlaybackSettings.qml`。

## 2. 媒体地址、缓存身份和解码边界

fork 不把 Bilibili 流伪装成网易云的内容资源：

- `MediaSource` 增加显式的 provider、内容寻址/流寻址信息，并保存 Bilibili 视频、分 P 和流身份；音频缓存按实际流身份区分，不用伪造网易云式 MD5 代替来源身份。
- 媒体下载按 provider 校验 URL、重定向和请求边界。网易云地址限制为 HTTPS 的 `music.126.net`；Bilibili 只允许规定的 CDN 域族和端口，并使用需要的 Referer/User-Agent。媒体来源对象不携带 Cookie。
- 解码路径按声明的容器和 codec 选择 demuxer、codec 白名单并核对实际 codec；支持 MP4 中的 AAC/FLAC 等 fork 使用场景。Fedora 的 E-AC-3 由独立的私有实现处理，其他格式仍按系统音频链路处理。
- 音源切换、预载退休和异步下载回调都带会话/播放身份检查，旧音源的下载结果不能覆盖新音源。

主要实现：`src/domain/playback/media_source.h`、`src/infrastructure/media/media_source_policy.h`、`src/infrastructure/media/audio_store.*`、`src/infrastructure/media/audio_decoder.*`、`src/infrastructure/bilibili/bili_stream_resolver.*`。

## 3. 按实际播放源选择歌词

上游主要按歌曲请求网易云歌词；fork 将歌词与实际播放源绑定：

- 使用网易云音源时使用网易云歌词。
- 使用 Bilibili 音源时优先读取当前匹配视频及分 P 的 Bilibili 字幕，通过 Bilibili 播放器接口查询字幕并优先选择中文。
- 字幕 URL 只接受符合 HTTPS、域名和 URL 结构约束的 `hdslb.com` 地址。
- Bilibili 字幕保留逐行起止时间，不把逐行字幕伪造成逐字时间。
- 只有确认字幕不存在或有效字幕正文为空时，才请求网易云歌词；字幕网络错误、拒绝或格式损坏不会统一被当成“没有字幕”而静默回退。
- 歌词请求绑定播放 token、provider 和具体视频/分 P；切歌、切源或切账号后，旧回调不能覆盖当前歌词。

主要实现：`src/infrastructure/bilibili/bili_lyrics.*`、`src/application/playback/listening_session.*`、`src/infrastructure/runtime/backend_runtime.*`、`ui/presentation/lyrics.*`、`ui/presentation/backend/backend_playback.*` 和 `ui/player/LyricsView.qml`。

## 4. 播放记录与“一起听”的边界

- Bilibili 是实际播放源时，不把该播放过程伪装成网易云音乐播放，不向网易云上报该 Bilibili 播放记录；这不等于完全关闭本地播放记录。
- Bilibili 音源不加入网易云“一起听”会话；一起听和播客队列使用不支持辅助音源的边界。
- 进入或恢复一起听会话时，会重新核对当前来源；发现不支持的来源则结束或阻止该会话，而不是继续使用错误的 provider。
- 普通队列和私人 FM 的连续失败计数由统一策略处理；达到上限时停止并通知，观察到有效播放后清除连续失败状态。

相关实现位于 `src/application/playback/listening_reporter.*`、`src/application/playback/listening_session.*`、`src/application/playback/listening_fm.*`、`src/application/playback/together_session.*` 和 `ui/presentation/backend/backend_playback.*`。

## 5. Linux 播放状态和歌词导出

fork 对上游已有的 Linux 本地播放状态接口做了下游适配，而不是另建一套控制接口：

- Unix socket 接口只读，默认关闭，需要用户在设置中明确启用；播放控制仍使用 MPRIS。
- 导出的播放快照和歌词与实际 provider、播放代次和当前歌词文档一致，包含 Bilibili 的逐行字幕。
- 客户端准入、首次有效播放观察、socket 权限、私有运行目录和自有进程清理均按当前播放会话处理；旧会话不能继续向新会话客户端提供数据。
- 当前 fork 不再同时提供旧的 `org.cirrus.Lyrics1` 或 MPRIS `xesam:asText` 歌词导出；该 socket 是当前的歌词外部导出方式。
- 该导出功能为 Linux 专属，Windows/macOS 不提供相同 socket 接口。

协议和客户端见 [`tools/now-playing/README.md`](../tools/now-playing/README.md)。相关实现：`src/infrastructure/ipc/`、`src/application/playback/playback_observation_publisher.*`、`ui/presentation/backend/now_playing_export.*`。

## 6. 云盘上传和管理链路

fork 有意不提供上游云盘管理与本地音乐上传功能，相关链路是删除而非仅隐藏页面：

- 删除云盘页面、上传面板、云盘模型和对应 presentation 接口。
- 删除云盘上传、删除、匹配/纠错、上传检查、上传分配、上传信息和发布等 API、gateway、领域类型及实现。
- 删除对应云盘写入测试、上传时退出确认和与这些写操作绑定的 UI 阻挡逻辑。
- 保留网易云普通歌单、红心、收藏等必要功能；使用喜欢歌单或心动模式时不恢复云盘写入链路。

涉及 `ui/app/Cloud*.qml`、`ui/presentation/cloud_models.*`、`src/domain/music/cloud_upload_types.h`、`src/infrastructure/netease/music/cloud_upload.*`、相关 collection API/gateway 以及 `tests/unit/cloud_writes_test.cpp` 等文件。

## 7. 跨平台缓存和私有文件保护

上游已有缓存体系，fork 重点补齐缓存初始化和安全访问约束：

- 资源、音频、封面、歌曲信息和歌词缓存按账号隔离，并可跨应用重启复用。
- 创建目录时即设置私有权限；已有目录则检查实际所有者、权限和路径类型，不安全时拒绝使用，不静默切换到另一个位置。
- Unix 检查 owner、目录/文件 mode 和符号链接；Windows 检查 ACL、owner 和 reparse point 等路径重定向风险。
- 音频读取和摘要验证使用安全打开文件的路径，避免通过链接或不安全替换访问到缓存目录之外的内容。
- 缓存清理、重启、Unicode/空格路径、账号隔离和正在播放文件的生命周期都有对应回归测试。

主要实现：`src/infrastructure/cache/`、`src/infrastructure/storage/private_files.*`、`ui/presentation/backend/backend_host.*`、`tests/unit/runtime_cache_test.cpp`、`tests/unit/storage_test.cpp` 和 `tests/unit/backend_runtime_test.cpp`。

## 8. 正文字体与图标字体

- 移除随应用捆绑的 MiSans 可变字体、许可证资源、下载/注册逻辑、MiSans 字重映射及专用设置选项。
- 正文和歌词默认使用系统字体；用户可以选择已安装字体，或导入并手动选择字体文件。
- 字体设置使用明确的 System/Custom 来源和自定义字体文件；不把旧 MiSans 设置自动迁移到其他字体，旧用户需要重新选择。
- Material Symbols 仍随应用提供且只用于图标。修改图标集合时编辑 `ui/theme/fonts/symbols.txt`，再由 `tools/ui/prepare_fonts.py` 从固定上游来源生成子集字体和 `Symbols.qml`，不手工替换生成文件。

主要文件：`ui/theme/fonts.*`、`ui/theme/fonts/README.md`、`ui/theme/fonts/symbols.txt`、`ui/theme/Symbols.qml`、`tools/ui/prepare_fonts.py` 和 `ui/app/settings/AppearanceSettings.qml`。

## 9. Linux Qt、QtKeychain 和 FFmpeg 依赖策略

fork 的 Linux 包不把另一套 SDK 的 Qt、QtKeychain、ICU 或普通 FFmpeg DSO 夹带进应用，而是使用目标发行版的系统依赖：

| 发行版 | 上游与 fork 的交付差异 | fork 特化 |
| --- | --- | --- |
| Arch Linux | 使用标准 `.pkg.tar.zst` 原生包和系统运行库，不再使用旧 AUR/AppImage 入口。 | 原生 x86_64/aarch64 构建；包安装到标准系统路径。 |
| Debian 13 | 使用 `.deb`，Qt、QtKeychain、FFmpeg 和 ICU 由 Debian 系统包/Qt 依赖管理。 | 只在 Debian 应用工作树应用 Qt 6.8 兼容补丁；系统 Qt 达到补丁要求的更高版本时明确失败并要求移除补丁、接入新 API；公共主线 Qt 要求不下调。 |
| Fedora 44 | 使用 `.rpm` 和 Fedora 官方 `ffmpeg-free`，不要求 RPM Fusion。 | 只在 Fedora 应用工作树应用 E-AC-3 接入补丁；E-AC-3 使用私有最小 FFmpeg 静态闭包，其余格式走系统 FFmpeg。 |

Debian/Fedora 的包元数据根据 ELF 实际依赖、Qt 插件/QML 文件归属和发行版包名生成/核对。Fedora 私有 `libcirrus_eac3.so.1` 只在 E-AC-3 codec ID 路径使用，通过固定 C ABI、隐藏符号和版本脚本隔离内部 FFmpeg，不成为全局 FFmpeg 替代品。

实现与规则：`packaging/linux/debian/patches/0001-cirrus-debian-qt68.patch`、`packaging/linux/fedora/patches/0001-cirrus-fedora-eac3-backend.patch`、`packaging/linux/fedora/`、`packaging/linux/verify-system-qt.sh`、`packaging/linux/verify-system-ffmpeg.sh`、`packaging/linux/qt-runtime-dependencies.py` 和 `packaging/linux/build-common.sh`。

Windows/macOS 不套用上述 Linux 系统依赖规则，仍按各自原生 SDK/依赖准备和部署运行时；产物分别为 Windows ZIP/安装器和 macOS DMG。

## 10. 构建、测试和发布工作流

fork 将测试和生产打包拆成不同的原生工作流：

- `ci.yml` 覆盖 Arch、Debian、Fedora 各 x86_64/aarch64，Windows x86_64/ARM64，macOS x86_64/ARM64，共十个原生测试目标；手动运行可筛选平台和架构，分支 push 使用完整矩阵。
- `linux-build.yml`、`windows-build.yml`、`macos-build.yml` 负责生产包。Linux 可选择发行版和架构；Windows/macOS 可选择架构；构建在匹配的原生 runner 上进行，不使用跨架构模拟。
- Linux 独立构建只负责构建选中的发行版包，不通过查询其他 CI 状态来决定是否继续；Release 通过 job 依赖确保测试成功后才构建。
- PR 不自动触发专门的 `pull_request` 工作流；需要依赖分支 push 或手动/可复用工作流结果。
- Linux 平台 artifact 只包含一个对应的 `.pkg.tar.zst`、`.deb` 或 `.rpm`；Windows artifact 包含产品 ZIP 和安装器；macOS artifact 包含 DMG。Actions 下载产生的外层 ZIP 只是 artifact 容器，不代表 Linux 产品是便携 ZIP。
- `Cirrus-<版本>-library-sources.tar.xz` 作为独立的 `cirrus-library-sources` artifact 上传，不混入 Arch、Debian 或 Fedora 平台安装包 artifact。
- Release 固定源码、校验版本和标签，等待三平台测试与构建完成后检查平台/架构资产、文件名、数量、空文件、重复项和 SHA-256，再发布资产。

主要文件：`.github/workflows/ci.yml`、`.github/workflows/linux-build.yml`、`.github/workflows/windows-build.yml`、`.github/workflows/macos-build.yml`、`.github/workflows/release.yml`、`.github/actions/setup-*/action.yml` 和 `tools/ci/select-native-matrix.py`。

## 11. 更新来源、工程资源和旧入口

- 应用内更新查询 fork 自己的 GitHub Releases，而不是上游仓库；Windows 安装器按应用构建架构选择 x64 或 ARM64 资产，并校验下载包 SHA-256。
- 截图从 `packaging/screenshots/` 整理到 `resources/screenshots/`；第三方许可证原文集中到 `third_party/licenses/`；依赖版本和源码获取脚本整理到 `packaging/`。
- 上游根目录的 `CHANGELOG.md` 不再作为另一份并行发布日志；当前发布说明位于 `resources/CHANGELOG.md`。
- 删除旧 `packaging/aur/PKGBUILD`、旧 Linux 多发行版打包入口及与当前发布策略不一致的旧平台工作流；Arch 使用当前唯一的源码构建入口，并由 `packaging/linux/arch/cpkg/` 对接现有 cpkg 发布体系。
- 删除仓库内旧 `.vscode` 项目配置；README 只保留简短差异概览，详细差异集中在本文，构建发布细节集中在 `packaging/README.md`。
