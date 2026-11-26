# 打包与发布

**该操作需要通过 GitHub Actions 执行。** 编译、自动化测试、打包和发布均在 Actions 中完成。安装包只包含应用及运行依赖，不包含测试和 gallery。

## 目录

```text
packaging/
├── linux/
│   ├── arch/
│   │   └── cpkg/
│   ├── debian/
│   │   └── patches/
│   └── fedora/
│       └── patches/
├── windows/
└── macos/
```

Linux 通用编译、部署、桌面资源和依赖安装直接放在 `packaging/linux/`；发行版打包入口放在各自子目录。跨平台依赖脚本和版本清单位于 `packaging/` 根目录；许可证原文位于 `third_party/licenses/`，截图位于 `resources/screenshots/`。Arch 始终使用标准 `.pkg.tar.zst` 后缀。

## 工作流分工

| 工作流 | 入口 | 职责 |
| --- | --- | --- |
| `ci.yml` | 所有分支 push、手动、可复用调用；PR 不触发 | Arch、Debian 13、Fedora 44 各 x86_64/ARM64，Windows、macOS 各 x86_64/ARM64，共十个原生测试目标；手动运行可选 `target` 和 `architecture`，push 仍测试完整矩阵 |
| `linux-build.yml` | 手动、可复用调用 | Arch、Debian、Fedora × x86_64、ARM64，按 `distribution` 和 `architecture` 选择目标；不查询 CI 状态。Release 通过 `needs: [check, ci]` 保证测试成功后再启动构建 |
| `windows-build.yml` | 手动、可复用调用 | 按 `architecture` 选择 x86_64、ARM64 两个原生构建目标，各输出 ZIP 和安装器 |
| `macos-build.yml` | 手动、可复用调用 | 按 `architecture` 选择 ARM64、x86_64 两个原生构建目标，各输出 DMG |
| `release.yml` | 手动选择 `ref`、`tag`、`publish` | 检查版本、Tag 和 Changelog 并固定源码提交 → CI 测试 → 并行构建三平台十个原生目标 → 汇合校验完整产物清单和 SHA-256 → 发布 |

CI 分为 `linux-tests`、`windows-tests`、`macos-tests` 三个独立 job 定义，分别通过经校验的动态矩阵展开为 6、2、2 个原生任务。手动运行 `ci.yml` 时可用 `target` 选择 `all`、`linux`、`arch`、`debian`、`fedora`、`windows` 或 `macos`，并用 `architecture` 选择 `all`、`x86_64` 或 `arm64`；例如 `target=arch`、`architecture=x86_64` 只申请 Arch x86_64 原生 runner。分支 push 不受输入筛选，始终运行全部十个目标。

生产构建继续按需手动运行，不随每次 push 自动启动。独立 Linux 构建仅校验发行版/架构选择并固定要构建的完整源码 SHA，不查询 CI 状态；Release 仍由自身工作流依赖关系保证 CI 全部通过后才启动构建。Linux 手动参数为 `distribution=all|arch|debian|fedora` 与 `architecture=all|x86_64|arm64`；Windows/macOS 工作流各以 `architecture=all|x86_64|arm64` 筛选。输入值经共享脚本校验，非法组合直接失败，不会静默生成空矩阵。

例如，在 Actions 手动选择工作流时选择相同字段；也可用 GitHub CLI：

```sh
gh workflow run ci.yml --ref feat/debian-system-qt -f target=arch -f architecture=x86_64
gh workflow run linux-build.yml --ref feat/debian-system-qt -f distribution=arch -f architecture=x86_64
gh workflow run windows-build.yml --ref feat/debian-system-qt -f architecture=arm64
gh workflow run macos-build.yml --ref feat/debian-system-qt -f architecture=x86_64
```

`gh workflow run --ref` 选择工作流所在分支；CI 手动运行以此提交作为测试源码。三个独立构建工作流的可选输入 `ref` 指定实际被打包的源码，留空时使用该次工作流选定的提交。

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

十组目标共十二个二进制安装包。Debian x86_64 job 还收集一份 `Cirrus-<版本>-library-sources.tar.xz`，包含固定版本的 FFmpeg、QtKeychain 原始源码，供全部随包部署目标共用；其中 FFmpeg 原始源码供 macOS 和 Fedora 的最小 E-AC-3 库共用。

版本唯一来源为 `CMakeLists.txt` 中的 `project(CirrusMusic VERSION x.y.z)`；Release 标签必须为 `vx.y.z`，说明来自 [resources/CHANGELOG.md](../resources/CHANGELOG.md) 的唯一版本章节；该文件只保留当前待发布版本。版本号相同的分支包可能包含不同提交，下载时核对运行来源和提交 SHA。

## Linux 原生环境

Linux 生产包的配置、编译和打包命令都在对应发行版容器中执行；Ubuntu runner 仅承载这些容器。x86_64 使用 `ubuntu-24.04` 宿主，ARM64 使用 `ubuntu-24.04-arm` 原生宿主，不使用 QEMU 或跨架构编译。

| 目标发行版 | 构建容器 | 应用依赖 |
| --- | --- | --- |
| Arch x86_64 | 官方 `archlinux:base-devel` | 原生 Arch Qt、FFmpeg、QtKeychain |
| Arch Linux ARM | 社区 [Menci/docker-archlinuxarm](https://github.com/Menci/docker-archlinuxarm) 的 base-devel 镜像 | 原生 aarch64 Arch Linux ARM 仓库依赖 |
| Debian 13 | 官方 `debian:13`，两个原生 CPU job | 系统 Qt 6.8、QtKeychain、FFmpeg；应用工作树应用 Debian Qt 兼容补丁 |
| Fedora 44 | 官方 `registry.fedoraproject.org/fedora:44`，两个原生 CPU job | 系统 Qt、QtKeychain、FFmpeg；保留私有 E-AC-3 应用构建补丁 |

Arch ARM 镜像不是 Arch Linux ARM 官方发布镜像。当前固定 `base-devel-20261124.45339717499` 的 ARM64 manifest：`sha256:89dd0b1da3cab11ef9a0f12481794cf31fd2cb7377ced80aa2041d3a5cae757d`。更新镜像时应重新核对维护来源和原生架构。

依赖安装统一由 `packaging/linux/install-build-deps.sh` 按发行版和用途执行。生产构建使用默认 `purpose: build`；CI 明确指定 `purpose: tests`，在同一次包管理事务中安装开发依赖和测试服务，避免 Fedora 先自动选择冲突的 Pulse 服务 provider。容器启动时设置 `no_new_privs`，使 pacman 能在没有 `CAP_SYS_ADMIN` 的容器中应用 Landlock。每个构建 job 检查 `/etc/os-release` 与 `uname -m`，安装依赖后以无特权的 `cirrus-builder` 用户构建。

- **Arch**：对检出提交做 `git archive`，以 SHA-256 验证源码，渲染 `packaging/linux/arch/PKGBUILD.in` 后调用 `makepkg`。装到 `/usr`，不打包系统 Qt/FFmpeg。配方同时支持 `x86_64`、`aarch64`，检查包版本、实际架构、桌面资源和许可证。关闭 LTO 和额外 debug 包；依赖 Arch 滚动仓库，Qt 私有接口升级后需要重新构建。
- **Debian**：装到 `/opt/cirrus`，Qt、QtKeychain、FFmpeg 使用官方系统库；桌面集成和文档放到 `/usr/share`，`/usr/bin/cirrus` 链接到主程序。仅在独立应用构建工作树应用 `packaging/linux/debian/patches/0001-cirrus-debian-qt68.patch`：以现有 `mkdir(0700)` 安全原语按序创建缺失父目录，用旧版行过滤通知、字节/帧换算、列表输入兼容组件及独立音频事件线程的 `QIODevice` 推送接口适配 Qt 6.8。音频只在后端可写容量内生成 PCM，至多保存一个固定输出块的短写余量并计入延迟，关闭使用 `reset()` 避免暂停流 drain 阻塞；列表保留鼠标不拖动、触摸和滚轮可滚动的策略。主线最低 Qt 要求与其他平台实现不变；当 Debian 系统 Qt 达到这些 API 所需的 6.11 时，构建明确失败并要求移除补丁及接入，不静默切换路径。CI 对两个原生架构运行补丁后的完整测试、QML lint、音频短写/独立消费、私有虚拟 PulseAudio 暂停/关闭/重开及离屏列表输入测试。虚拟音频服务仅为测试构建依赖，不加入应用运行依赖。`dpkg-shlibdeps` 生成共享库和必要 Qt 私有 ABI 依赖；目标为 Debian 13。
- **Fedora**：同一应用安装布局，使用 Fedora 官方 `ffmpeg-free` 开发库，不启用 RPM Fusion。仅在 Fedora 构建的独立工作树中应用 `packaging/linux/fedora/patches/0001-cirrus-fedora-eac3-backend.patch`，为 Cirrus 接入内置 E-AC-3 解码库；FFmpeg 上游源码不打补丁，非 Fedora 构建不应用这份补丁。由 `rpmbuild` 生成 RPM；保留系统库的自动 ABI 依赖，排除随包私有 SONAME 依赖，并禁止把私有库发布为全局 Provides。

所有 Linux 目标通过系统 pkg-config 查找 FFmpeg，要求 5.1 起的新声道布局 API；不从 Qt SDK 或私有依赖前缀查找。CI 核对四个开发模块的系统路径和包所有权，并用发行版 FFmpeg 实际解码 MP3、FLAC、MP4/AAC、fragmented MP4 和 MP4/FLAC 样本；这些样本测试不在构建工作流运行。

Linux AudioEngine 集成测试与 Debian 音频后端回归共用一个私有 PulseAudio/null-sink 夹具，在无物理声卡的容器中执行播放、seek、暂停、衔接和停止用例。音频服务、会话 D-Bus 与运行目录均由测试进程独占，显式选择 PulseAudio 后端并验证唯一的虚拟输出；依赖或设备缺失直接失败。测试所需的 PulseAudio、会话 D-Bus 及 QML/离屏模块由 CI 步骤显式准备；生产构建不再显式安装这些测试服务，应用运行依赖也不增加测试服务。

公共播放代码、B 站 Dolby 请求与原有格式策略保持不变。Fedora 补丁按实际 codec ID 将 E-AC-3 交给 `libcirrus_eac3.so.1`，其余编码仍走原有系统 FFmpeg 路径，不进行失败重试或音源切换。该私有库由未修改的 FFmpeg 9.0.2 静态对象组成，配置仅启用 E-AC-3 及其上游强制的 AC-3/解析器依赖；应用入口只接受 E-AC-3，不把 AC-3 转给它。库以隐藏符号、版本脚本及本地绑定隔离内部 FFmpeg，边界仅传原始 packet 字节、时间戳及立体声 PCM，不交换 FFmpeg 对象。CI 的两个 Fedora 原生测试 job 均运行应用补丁后的音频测试，正向验证 E-AC-3、seek/reset 和实际后端选择，同时核验普通 AAC/FLAC 仍走系统库。

Debian/Fedora 不再下载或部署 Qt SDK、自编 QtKeychain、ICU、QML 模块及 Qt 插件。依赖准备核对系统 Qt/QtKeychain 的真实文件和发行版包所有权；打包检查包内没有这些共享库、插件/QML 目录、`qt.conf` 或独立 FFmpeg DSO。QML 导入由系统 `qmlimportscanner` 扫描；scanner 与 `qtpaths` 均使用本次 CMake 选中的 Qt 工具目标绝对路径，不依赖发行版的 PATH 别名，并按 Qt 自身 `qmldir` 的可选声明处理平台样式；源码显式导入和模块必需导入缺失仍会失败。OpenSSL TLS、SQLite、WebP/SVG、XCB/Wayland 和多媒体插件按文件所有权转换为原生运行包依赖，补足共享库扫描看不到的动态加载。CI 的六个 Linux 原生 job 将绑定完整源码提交的系统 Qt 版本、扫描器路径、QML/插件文件与原生包所有权证据合并到各自的 `test-results-Linux-*` 测试报告中，保留 3 天，不另建审计 artifact。生产构建复用同一依赖解析器生成 DEB/RPM 所需包名，不生成审计 JSON 或上传测试报告。ICU 由系统 Qt 依赖管理，Cirrus 不手写 ICU 版本；保留原生工具生成的必要 ABI 约束。四个 FFmpeg ELF 导入必须解析到系统库且进入 DEB/RPM 元数据。Fedora 仍单独检查唯一私有 E-AC-3 库的 C ABI、FFmpeg 符号隔离、独立性及 TEXTREL。保留全部 ELF 架构、缺库、构建目录泄漏、桌面文件与 AppStream 校验，只 strip 尚未去符号的文件。

Arch 只保留 `packaging/linux/arch/` 的当前源码构建链路，旧上游 AUR 配方已移除。`packaging/linux/arch/cpkg/` 负责将同一份二进制产物接入 `arch-cloud-pkgs` 的统一 OCI 发布器，不在包仓库重新构建 Cirrus。

## 共享依赖与缓存

`.github/actions/setup-qt` 仅为 Windows/macOS 从 `packaging/versions.sh` 读取 Qt 版本并安装 qtmultimedia、qtshadertools、qtimageformats 原生 SDK。全部 Linux 目标使用系统 Qt/QtKeychain；Linux CI 共用原生工作树准备脚本：Arch 使用未打补丁的源码，Debian/Fedora 应用各自的发行版补丁；生产构建保留同一份补丁准备入口。容器中的完整测试由非 root 用户运行，测试构建目录位于该用户 home，避免 GitHub 宿主挂载目录的异主祖先污染缓存安全测试；应用的所有者、权限和链接校验不变。

| 库 | 随包目标的来源 |
| --- | --- |
| Qt | Linux 由发行版提供；Windows/macOS 固定 Qt 6.11.2 官方二进制，macOS `clang_64` 含两个 CPU slice |
| FFmpeg | Linux 主解码器使用各发行版官方共享库；仅 Fedora 通过应用构建补丁增加隔离的最小 E-AC-3 库。macOS 从固定 SHA-256 的 9.0.2 源码编译；Windows 由固定 baseline 的 vcpkg 构建，保持原有配置 |
| QtKeychain | Linux 由发行版提供；Windows/macOS 从固定 0.17.0 源码针对选定 Qt 构建，Windows 静态链接 |
| OpenSSL、zlib、zstd | Linux 用对应发行版；Windows 用 vcpkg；macOS 用 Homebrew 的 OpenSSL/zstd 和系统 zlib |

Windows/macOS 的 Qt Multimedia 与 Cirrus 解码器可能各有不同 SONAME 的 FFmpeg；Linux 不部署 SDK FFmpeg。部署检查按实际 ELF/DLL/Mach-O 依赖验证。Linux/Windows/macOS 的依赖准备分别只有一个 setup action，由 CI 和生产构建共同调用；测试和生产的 CMake 配置分开。

依赖缓存严格区分发行版、CPU、Qt/工具链和依赖脚本。编译缓存按原生环境与测试/生产用途隔离，恢复后由 ccache 核验编译器内容、参数和头文件；运行依赖或准备脚本调整不会清空仍有效的编译输出。依赖构建完成后立即保存，编译缓存即使后续检查失败也会保存。缓存缺失只重新执行同一来源的依赖构建，不改变源码或编译目标。

安装规则位于 `cmake/CirrusInstall.cmake`。`CIRRUS_DEPLOY_QT=ON` 仅用于 Windows/macOS；Linux 必须关闭。`CIRRUS_RUNTIME_DIRS` 仅用于 Windows 的额外运行库搜索。许可证和第三方声明随应用安装；登录状态持久保存需要可用且已解锁的系统钥匙串。

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
