# 打包与发布

**该操作需要通过 GitHub Actions 执行。** 编译、自动化测试、打包和发布均在 Actions 中完成。安装包只包含应用及运行依赖，不包含测试和 gallery。

## 目录

```text
packaging/
├── linux/
│   ├── arch/
│   │   └── cpkg/
│   ├── debian/
│   └── fedora/
├── windows/
└── macos/
```

Linux 通用编译、部署、桌面资源和依赖安装直接放在 `packaging/linux/`；发行版打包入口放在各自子目录。跨平台依赖脚本和版本清单位于 `packaging/` 根目录；许可证原文位于 `third_party/licenses/`，截图位于 `resources/screenshots/`。Arch 始终使用标准 `.pkg.tar.zst` 后缀。

## 工作流分工

| 工作流 | 入口 | 职责 |
| --- | --- | --- |
| `ci.yml` | master push、PR、手动、可复用调用 | Ubuntu 24.04 x86_64、Windows x86_64、macOS ARM64 三个测试 job；编译测试所需目标、QML lint、CTest，仅上传测试报告 |
| `linux-build.yml` | 手动、可复用调用 | Arch、Debian、Fedora × x86_64、ARM64，矩阵展开为六个独立构建 job |
| `windows-build.yml` | 手动、可复用调用 | x86_64、ARM64 两个独立构建 job，各输出 ZIP 和安装器 |
| `macos-build.yml` | 手动、可复用调用 | ARM64、x86_64 两个独立构建 job，各输出 DMG |
| `release.yml` | 手动选择 `ref`、`tag`、`publish` | 检查版本、Tag 和 Changelog 并固定源码提交 → CI 测试 → 并行构建三平台十个原生目标 → 汇合校验完整产物清单和 SHA-256 → 发布 |

构建工作流没有 `package`、`format` 或平台开关；CI 不再承担打包任务。旧 Linux portable/AppImage 打包链路已移除。独立工作流的 `ref` 留空时使用所选工作流提交；Release 始终向 CI 和三个 build 工作流传入同一个完整提交 SHA。

在 Actions 选择 **Linux build / Windows build / macOS build → Run workflow**，选择工作流所在分支，并按需填写源码 `ref`。每次构建该平台的全部目标架构。新文件尚未在默认分支注册时，可通过已注册的 Release 工作流在功能分支上以 `publish=false` 调用和验证。

## 产物

| 目标 | 文件名 | Actions artifact |
| --- | --- | --- |
| Arch x86_64 | `cirrus-<版本>-1-x86_64.pkg.tar.zst` | `cirrus-linux-arch-x86_64` |
| Arch Linux ARM | `cirrus-<版本>-1-aarch64.pkg.tar.zst` | `cirrus-linux-arch-arm64` |
| Debian x86_64 | `cirrus_<版本>_amd64.deb` | `cirrus-linux-debian-x86_64` |
| Debian ARM64 | `cirrus_<版本>_arm64.deb` | `cirrus-linux-debian-arm64` |
| Fedora x86_64 | `cirrus-<版本>-1.fc44.x86_64.rpm` | `cirrus-linux-fedora-x86_64` |
| Fedora ARM64 | `cirrus-<版本>-1.fc44.aarch64.rpm` | `cirrus-linux-fedora-arm64` |
| Windows x86_64 | `Cirrus-<版本>-windows-x64.zip`、`…-x64-setup.exe` | `cirrus-windows-x86_64` |
| Windows ARM64 | `Cirrus-<版本>-windows-arm64.zip`、`…-arm64-setup.exe` | `cirrus-windows-arm64` |
| macOS ARM64 | `Cirrus-<版本>-macos-arm64.dmg` | `cirrus-macos-arm64` |
| macOS x86_64 | `Cirrus-<版本>-macos-x86_64.dmg` | `cirrus-macos-x86_64` |

十组目标共十二个二进制安装包。Debian x86_64 job 还收集一份 `Cirrus-<版本>-library-sources.tar.xz`，包含固定版本的 FFmpeg、QtKeychain 原始源码，供全部随包部署目标共用；其中 FFmpeg 源码用于 macOS，Linux 不编译或随包部署 FFmpeg。

版本唯一来源为 `CMakeLists.txt` 中的 `project(CirrusMusic VERSION x.y.z)`；Release 标签必须为 `vx.y.z`，说明来自 [resources/CHANGELOG.md](../resources/CHANGELOG.md) 的唯一版本章节；该文件只保留当前待发布版本。版本号相同的分支包可能包含不同提交，下载时核对运行来源和提交 SHA。

## Linux 原生环境

Linux 生产包的配置、编译和打包命令都在对应发行版容器中执行；Ubuntu runner 仅承载这些容器。x86_64 使用 `ubuntu-24.04` 宿主，ARM64 使用 `ubuntu-24.04-arm` 原生宿主，不使用 QEMU 或跨架构编译。

| 目标发行版 | 构建容器 | 应用依赖 |
| --- | --- | --- |
| Arch x86_64 | 官方 `archlinux:base-devel` | 原生 Arch Qt、FFmpeg、QtKeychain |
| Arch Linux ARM | 社区 [Menci/docker-archlinuxarm](https://github.com/Menci/docker-archlinuxarm) 的 base-devel 镜像 | 原生 aarch64 Arch Linux ARM 仓库依赖 |
| Debian 13 | 官方 `debian:13`，两个原生 CPU job | 固定 Qt SDK、自编 QtKeychain、Debian 官方 FFmpeg 与系统基础库 |
| Fedora 44 | 官方 `registry.fedoraproject.org/fedora:44`，两个原生 CPU job | 固定 Qt SDK、自编 QtKeychain、Fedora 官方 FFmpeg 与系统基础库 |

Arch ARM 镜像不是 Arch Linux ARM 官方发布镜像。当前固定 `base-devel-20261124.45339717499` 的 ARM64 manifest：`sha256:89dd0b1da3cab11ef9a0f12481794cf31fd2cb7377ced80aa2041d3a5cae757d`。更新镜像时应重新核对维护来源和原生架构。

依赖安装统一由 `packaging/linux/install-build-deps.sh` 按发行版执行。容器启动时设置 `no_new_privs`，使 pacman 能在没有 `CAP_SYS_ADMIN` 的容器中应用 Landlock。每个构建 job 检查 `/etc/os-release` 与 `uname -m`，安装依赖后以无特权的 `cirrus-builder` 用户构建。

- **Arch**：对检出提交做 `git archive`，以 SHA-256 验证源码，渲染 `packaging/linux/arch/PKGBUILD.in` 后调用 `makepkg`。装到 `/usr`，不打包系统 Qt/FFmpeg。配方同时支持 `x86_64`、`aarch64`，检查包版本、实际架构、桌面资源和许可证。关闭 LTO 和额外 debug 包；依赖 Arch 滚动仓库，Qt 私有接口升级后需要重新构建。
- **Debian**：装到 `/opt/cirrus`，自带 Qt/QtKeychain，FFmpeg 使用 Debian 官方共享库；桌面集成和文档放到 `/usr/share`，`/usr/bin/cirrus` 链接到主程序。`dpkg-shlibdeps` 从 Debian 包数据库生成外部 ABI 依赖，再添加证书、libsecret、图标主题依赖。目标是 Debian 13，不再声称兼容旧 Ubuntu 22.04 / Debian 12。
- **Fedora**：同一应用安装布局，使用 Fedora 官方 `ffmpeg-free` 开发库，不启用 RPM Fusion。由 `rpmbuild` 生成 RPM；保留 FFmpeg 等外部库的自动 ABI 依赖，排除已随包部署的私有 SONAME 依赖，并禁止把私有库发布为全局 Provides。只产生一个对应原生架构的二进制 RPM。

所有 Linux 目标通过系统 pkg-config 查找 FFmpeg，要求 5.1 起的新声道布局 API；不从 Qt SDK 或私有依赖前缀查找。依赖准备阶段核对四个开发模块的系统路径和包所有权，并用发行版 FFmpeg 实际解码 MP3、FLAC、MP4/AAC、fragmented MP4 和 MP4/FLAC 样本。

网易云链路使用 MP3/FLAC，B 站辅助源使用普通 AAC/FLAC；请求不包含 Dolby 标志，解析时忽略 Dolby 音轨。所有平台统一拒绝 E-AC-3，不设置按平台切换的编码能力分支。测试保留有效 E-AC-3 样本，作为明确拒绝的负向用例；正常音轨、格式映射和音质选择仍完整验证。

Debian/Fedora 部署排除未使用的 Qt FFmpeg 媒体播放插件及所有 FFmpeg 共享库；Qt 音频设备集成保留 GStreamer，实际歌曲解码仍只有 Cirrus 的 FFmpeg 解码器。部署后检查包内没有 FFmpeg，四个实际 ELF 导入均解析到系统库，生成的 DEB/RPM 元数据也必须包含四个运行库依赖。保留全部 ELF 架构、缺库与构建目录泄漏校验；删除旧 SDK FFmpeg 专用的 bzip2 SONAME 改写。只 strip 尚未去符号的文件。桌面文件与 AppStream 元数据在打包时验证。

Arch 只保留 `packaging/linux/arch/` 的当前源码构建链路，旧上游 AUR 配方已移除。`packaging/linux/arch/cpkg/` 负责将同一份二进制产物接入 `arch-cloud-pkgs` 的统一 OCI 发布器，不在包仓库重新构建 Cirrus。

## 共享依赖与缓存

`.github/actions/setup-qt` 从 `packaging/versions.sh` 读取 Qt 版本，统一安装 qtmultimedia、qtshadertools、qtimageformats；按平台和 CPU 选择原生 SDK。Arch 使用系统 Qt，跳过 SDK 和自编库链路。

| 库 | 随包目标的来源 |
| --- | --- |
| Qt 6.11.2 | Qt 官方二进制；macOS `clang_64` 是同时含两个 CPU slice 的框架 |
| FFmpeg | Linux 使用各发行版官方共享库；macOS 从固定 SHA-256 的 9.0.2 源码编译；Windows 由固定 baseline 的 vcpkg 构建。应用支持 MP3、FLAC，以及 MP4/fMP4 中的 AAC/FLAC |
| QtKeychain 0.17.0 | 同一源码脚本，针对选定 Qt 构建；Windows 静态链接 |
| OpenSSL、zlib、zstd | Linux 用对应发行版；Windows 用 vcpkg；macOS 用 Homebrew 的 OpenSSL/zstd 和系统 zlib |

Windows/macOS 的 Qt Multimedia 与 Cirrus 解码器可能各有不同 SONAME 的 FFmpeg；Linux 不部署 SDK FFmpeg。部署检查按实际 ELF/DLL/Mach-O 依赖验证。Linux/Windows/macOS 的依赖准备分别只有一个 setup action，由 CI 和生产构建共同调用；测试和生产的 CMake 配置分开。

依赖缓存严格区分发行版、CPU、Qt/工具链和依赖脚本。编译缓存按原生环境与测试/生产用途隔离，恢复后由 ccache 核验编译器内容、参数和头文件；运行依赖或准备脚本调整不会清空仍有效的编译输出。依赖构建完成后立即保存，编译缓存即使后续检查失败也会保存。缓存缺失只重新执行同一来源的依赖构建，不改变源码或编译目标。

安装规则位于 `cmake/CirrusInstall.cmake`。`CIRRUS_DEPLOY_QT=ON` 用于 Debian、Fedora、Windows、macOS；Arch 关闭。`CIRRUS_RUNTIME_DIRS` 仅用于 Windows 的额外运行库搜索。许可证和第三方声明随应用安装；登录状态持久保存需要可用且已解锁的系统钥匙串。

## Windows 与 macOS

Windows x86_64 使用 `windows-2022` 原生 MSVC，ARM64 使用 `windows-11-arm` 原生 MSVC；vcpkg 的目标与 host triplet 均匹配 CPU。MSVC CRT 来源固定为当前 `VCToolsRedistDir`、编译器目标 ABI 与工具集版本对应的目录；`runtime.py` 从主程序和全部插件的真实 DLL 导入闭包部署所需 CRT，逐一核对架构和来源，不复制整组候选文件，也不以构建机预装 CRT 满足包内依赖。主程序及部署 DLL 检查 PE 架构、运行依赖和必要运行库。安装器 `AppId` 保持一致，x64 与 ARM64 目录/文件名及最低系统要求分别生成。更新器根据应用构建架构选择对应安装器，仍验证 fork 下载地址和 GitHub SHA-256，不用另一个 CPU 的资产替代。

Windows 安装器未作发行者签名。x64 目标为 Windows 10/11；ARM64 目标为 Windows 11 22H2 及更新版本。

macOS ARM64 使用 `macos-15`，x86_64 使用 `macos-15-intel`，均原生编译并要求 macOS 15。`macdeployqt` 部署依赖，脚本按目标 CPU 检查全部 Mach-O、拒绝构建机路径，然后 ad-hoc 签名并验证，再生成含 `/Applications` 链接的 DMG。两种架构均为预览，未经 Apple 公证；云端检查不代替用户真机安装和桌面交互测试。

## 发布与整体验证

1. 先合入配套 `arch-cloud-pkgs` 更新（标准后缀、Cirrus 目录、双架构 OCI 读取和统一发布器），再发布 Cirrus。其 `main` 是正式 cpkg 发布器来源。
2. 在待发布源码中更新版本，并重写 `resources/CHANGELOG.md`，仅保留这个版本的非空发布说明，不保留 Unreleased 或旧版本章节，然后提交并推送；来源必须包含本套构建工作流和脚本。
3. 在 Actions 运行 **Release**，填写来源 `ref`、匹配的 `tag` 和 `cpkg_ref`（默认 `main`）。`publish=false` 可用包仓库验证分支/提交，验证同一批真实包及 OCI 元数据，不写入 GHCR 或 GitHub Release。验证运行按工作流提交隔离；正式发布始终按 tag 串行。
4. `publish=true` 仅接受 `cpkg_ref=main`。`check` 检查发布版本、Tag，以及 `resources/CHANGELOG.md` 只有当前版本且说明非空后，CI 按锁定的完整源码 SHA 运行三平台测试；只有 CI 全部通过，才并行启动六个 Linux、两个 Windows、两个 macOS 构建目标。最终发布 job 等待三个平台全部构建成功；任一检查、测试或构建失败，发布 job 不运行。
5. 发布 job 精确核对十组 artifact、十二个二进制包和一份库源码，不接受遗漏、额外文件、空文件或重名；生成 `BUILD-METADATA.json` 与 `SHA256SUMS.txt`。
6. 同一批 Arch 包由 `packaging/linux/arch/cpkg/publish.sh` 交给包仓库的统一发布器，形成 `ghcr.io/wxmyyds/cirrus` 双架构 OCI index；cpkg 按本机架构取包。
7. 若目标标签已经存在，必须仍指向选定的源码提交；正式发布包前保留一次指向校验，避免构建期间的 Tag 变化影响发布。所有资产先上传到草稿，完整上传成功后才正式发布；新标签指向锁定的源码提交。标签 push 不再自动触发 Release。

GHCR 首次创建包默认是 Private。首次正式发行后，需要包所有者将 Cirrus 包可见性设为 Public；工作流验证匿名访问，不能匿名拉取时明确失败，不会把私有上传报告为可安装。调整可见性后可重跑失败的发布 job，继续使用原运行的同一批构建产物。详见 `linux/arch/cpkg/README.md`。

`release-manifest` artifact 保存版本、源码提交、资产清单和校验值，保留 7 天；测试报告保留 3 天。二进制 artifact 保留 1 天，重跑时只替换当前运行的同名产物。完整 Release 和独立平台构建均不运行产物清理脚本；维护者按需手动删除旧 Actions 产物，清理时保留仍在运行或需要重跑验证的产物。

安装包可在保留期内下载；GitHub Release 资产、GHCR 包和编译缓存不属于此次 Actions 产物手动清理范围。单次完整构建的短时产物峰值仍可能超过 500 MB。验证此工作流不等于完成各平台安装或桌面运行回归。
