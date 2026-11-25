<p align="center"><img src="ui/app/icons/cirrus-256.png" width="128" alt="Cirrus"></p>

<p align="center">
  <a href="https://github.com/wxmyyds/CirrusMusic/stargazers"><img alt="Stars" src="https://img.shields.io/github/stars/wxmyyds/CirrusMusic?style=flat-square&logo=github" /></a>
  <a href="https://github.com/wxmyyds/CirrusMusic/releases"><img alt="Release" src="https://img.shields.io/github/v/release/wxmyyds/CirrusMusic?style=flat-square" /></a>
  <a href="https://github.com/wxmyyds/CirrusMusic/releases"><img alt="Downloads" src="https://img.shields.io/github/downloads/wxmyyds/CirrusMusic/total?style=flat-square" /></a>
  <a href="https://www.qt.io/"><img alt="Qt" src="https://img.shields.io/badge/Qt-6.10%2B-41CD52?style=flat-square&logo=qt&logoColor=white" /></a>
</p>

# Cirrus

本项目基于上游 [Guang233/CirrusMusic](https://github.com/Guang233/CirrusMusic) 的 **v0.5.0** 修改，是一个使用 C++ 和 Qt Quick 编写的非官方网易云音乐桌面客户端。保留上游的音乐浏览、播放引擎和界面，主要围绕 Bilibili 辅助音源、实际播放源字幕、桌面集成和多平台原生构建进行调整。

以下差异以已同步的[上游 v0.5.0](https://github.com/Guang233/CirrusMusic/releases/tag/v0.5.0) 为对照；上游后续变化以其仓库为准。本项目的当前发布说明见 [resources/CHANGELOG.md](resources/CHANGELOG.md)。

## 相比上游的改动

### 新增与调整

- **Bilibili 辅助音源**：默认关闭，可在播放设置中启用。网易云歌曲因权限限制无法完整播放，或只返回试听流时，尝试按歌名、歌手和时长匹配 B 站视频及分 P；普通网络错误不会触发切源。播放页会标明实际音源。
- **实际播放源字幕**：使用 Bilibili 音源时，读取选中视频及分 P 的字幕；字幕缺失或为空时使用网易云歌词。B 站字幕保留逐行时间，不伪造逐字时间。沿用上游 Linux Unix socket 接口，并适配实际音源的歌词与时序，让桌面歌词和应用显示一致。
- **Bilibili 凭据管理**：支持粘贴、校验和清除 Bilibili Cookie，通过系统钥匙串保存，不写入普通设置文件；需要可用的系统钥匙串服务。
- **独立更新来源**：应用内更新只查询本仓库的 GitHub Releases；Windows 安装版按当前 CPU 选择安装器并校验 SHA-256，避免被上游或其他架构的安装包覆盖。
- **原生多平台构建**：Linux 提供 Arch、Debian、Fedora 的 x86_64 / ARM64 原生包；Windows 提供 x64 / ARM64 的安装器和 ZIP；macOS 提供 Apple Silicon / Intel 的 DMG。构建与发布统一通过 GitHub Actions 执行。

### 未引入的功能与当前限制

- **不提供上游云盘管理功能**：没有云盘页面、本地音乐上传、云盘歌曲删除、纠错或取消关联入口。
- **Bilibili 音源存在使用边界**：匹配结果与音质取决于实际资源，不保证与网易云原曲完全一致；播放 B 站音源时不向网易云上报听歌记录，也不支持“一起听”。
- **详细播放状态与歌词导出目前仅支持 Linux**，默认关闭；Windows / macOS 没有对应的导出接口。
- **不再提供 AppImage 和旧 AUR 打包入口**，Linux 使用当前的原生发行版打包链路。

![首页](resources/screenshots/home-dark.png)

| 侧边播放器 | 播放页（Material 风格） |
| --- | --- |
| ![侧边播放器](resources/screenshots/side-player.png) | ![播放页（Material 风格）](resources/screenshots/player-material.png) |

| 播放页（亮色） | 播放页（暗色） |
| --- | --- |
| ![播放页（亮色）](resources/screenshots/player-light.png) | ![播放页（暗色）](resources/screenshots/player-dark.png) |

## 保留的上游功能

- **发现与搜索**：推荐歌单、歌单分类、排行榜、歌手、新碟，每日推荐与私人 FM；搜索歌曲、歌手、专辑、歌单、播客和声音。
- **音乐库**：创建和收藏的歌单、我喜欢的音乐、收藏的专辑和歌手、最近播放、听歌排行；红心、收藏歌单、关注、新建歌单。
- **播放**：自己的音频引擎，边下边播、精确跳转，歌曲缓存可设上限；音质选择、音量均衡；歌曲之间可以无缝衔接，或用智能混音按节拍和调性过渡。
- **播客**：播放播客里的节目和声音，收听记录同步到网易云。
- **播放页**：两种风格：随封面变化的流光背景，或整页取封面颜色的 Material 风格；逐字歌词、翻译和注音，可拖动排序的播放队列。
- **侧边播放器**：悬浮播放条可以停靠到窗口右侧，显示封面、播放控制，以及歌词或接下来的歌曲；播放条上的按钮可以自己选。
- **一起听**：和好友（网易云音乐 App 或 Cirrus）一起听同一份歌单，换歌、暂停、拖动进度互相同步。
- **听歌记录**：同步到网易云的最近播放、听歌排行和听歌足迹；另有本地的听歌统计看板。
- **分享**：复制歌单、专辑、歌手、播客、用户或歌曲的链接，文案可以自己改。
- **桌面集成**：
  - 媒体键和系统媒体控制（MPRIS）、托盘、单实例；
  - 网页上的“在客户端打开”交给 Cirrus 打开（Linux 默认，Windows 安装时可选）；
  - 可以把播放状态和带时间的歌词提供给桌面歌词、状态栏等程序（Linux，默认关闭，见[下文](#给其他程序读取播放状态)）；
  - 配色跟随封面或自选主题色，亮色和暗色；
  - 有新版本时提示（向 GitHub 查询，可关闭）。
- **登录**：扫码、手机号（验证码或密码）、邮箱；登录状态保存在系统钥匙串里。

## 从源码构建

**该操作需要通过 GitHub Actions 执行。** 在 Actions 选择待构建分支，再运行 `Linux build`、`Windows build` 或 `macOS build`，填写需要构建的源码 `ref`，完成后下载对应架构 artifact。

Linux 构建包含 Arch、Debian、Fedora 的 ARM64 与 x86_64 六个原生发行版 job；Windows 和 macOS 各两个原生架构 job。`CI` 独立执行 Linux、Windows、macOS 测试，不生成安装包。

发布使用 `Release` 工作流：指定源码 `ref` 和版本 `tag`，先检查版本、Tag 和 Changelog，并锁定完整源码提交；三平台 CI 测试全部通过后，再并行运行 Linux、Windows、macOS 的十个原生构建目标，全部成功后统一校验和发布。`publish=false` 可验证完整流程而不发行。细节见 [打包与发布](packaging/README.md)，代码结构见 [CONTRIBUTING.md](CONTRIBUTING.md)。

构建软件包保留 1 天，测试报告保留 3 天，Release 来源清单和校验值保留 7 天。本项目不运行产物清理脚本，旧 Actions 产物由维护者按需手动删除。

## 数据存放位置

以下是 Linux 上的位置；Windows 和 macOS 上在系统的用户数据目录里。

| 内容 | 位置 |
| --- | --- |
| 设置 | `~/.config/Cirrus/` |
| 播放队列、本地听歌记录 | `~/.local/share/Cirrus/` |
| 封面和歌曲缓存 | `~/.cache/Cirrus/`（可在 设置 › 存储 里清除） |
| 日志 | `~/.local/state/Cirrus/logs/` |
| 登录状态 | 系统钥匙串，名为 `com.guang233.Cirrus` |

## 给其他程序读取播放状态

在 Linux 上打开 设置 › 窗口与系统 › 向其他程序提供播放状态 (私货) 后，同一用户的其他程序可以通过本机的 Unix 套接字读取：

- 正在播放的歌（歌名、歌手、专辑）、进度和播放状态；
- 歌词：原文、翻译、罗马音，各有逐行和逐字，还有当前唱到的行和字。

这个开关默认关闭，接口只读，不能控制播放（控制请用 MPRIS），也不会因此多发任何网络请求。
这是本 fork 唯一的详细歌词导出接口：不再导出旧 `org.cirrus.Lyrics1` 或 MPRIS `xesam:asText`。
Bilibili 字幕与界面读取同一个实际播放会话的歌词文档，不伪造逐字时间。
发现清单写在私有运行时目录，歌词正文仅通过 socket 传输；关闭开关会断开客户端并删除自己的端点。
升级后请打开这个新开关，旧的“向外部提供歌词”设置不再使用。
适合用来做桌面歌词、waybar/polybar 的状态栏模块、OBS 叠加层等。协议说明和一个只用 Python 标准库的示例客户端见 [tools/now-playing](tools/now-playing/README.md)：

```bash
python3 tools/now-playing/cirrus_now_playing.py          # 每首歌一行标题，每句歌词一行
python3 tools/now-playing/cirrus_now_playing.py --once   # 打印一份快照（JSON）
```

## 反馈问题

本项目的问题请在 [Issues](https://github.com/wxmyyds/CirrusMusic/issues) 报告。遇到播放失败、加载不出来之类的情况，请在 **设置 › 诊断** 里生成一个诊断包附上：里面有日志和系统信息，不含账号凭据。

## 声明

- Cirrus 是个人开发的第三方客户端，与网易云音乐及网易公司没有任何关系，也未获得其授权或认可。“网易云音乐”是网易公司的商标。
- 音乐、歌词、封面等内容的版权归网易云音乐及各自的权利人所有。Cirrus 不提供、不存储也不分发这些内容，只播放你的账号本来就能播放的歌曲。
- 本项目仅供学习和个人使用，请勿用于商业用途。

## 致谢

- [Guang233/CirrusMusic](https://github.com/Guang233/CirrusMusic)：本项目的上游，提供客户端主体、播放引擎和界面基础。
- [end-4/dots-hyprland](https://github.com/end-4/dots-hyprland)（illogical-impulse）：界面里的许多控件借鉴了它的设计。
- [NeteaseCloudMusicApiEnhanced/api-enhanced](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced)：本项目的大部分接口来源于此。

## 许可证

[MIT](LICENSE)。安装包附带的第三方软件（Qt、FFmpeg、QtKeychain、MiSans 字体、Material Symbols 等）及其许可证见 [packaging/THIRD-PARTY-NOTICES.md](packaging/THIRD-PARTY-NOTICES.md)。
