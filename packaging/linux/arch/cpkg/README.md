# Cirrus → cpkg

这里负责把 Cirrus Release 已构建的两个 Arch 包接入 `wxmyyds/arch-cloud-pkgs` 的 cpkg 目录：

- `cirrus-<版本>-1-x86_64.pkg.tar.zst`
- `cirrus-<版本>-1-aarch64.pkg.tar.zst`

`publish.sh` 先核对 Release 的源码提交和 SHA-256，再调用检出的包仓库 `.github/scripts/publish-oci-package.sh`。OCI 编码、上传和不可变版本校验只有包仓库这一处实现；本目录不复制上传器，不重新构建或改名 Arch 包。

Release 使用 `packages: write` 的工作流令牌发布到 `ghcr.io/wxmyyds/cirrus`，无需个人令牌或跨仓库 Actions 写权限。包目录与 OCI 读取协议由 `arch-cloud-pkgs` 维护。两个架构使用同一版本的 OCI index，cpkg 按本机架构选择并验证对应包，不改选其他架构。

正式发布只使用包仓库 `main`。整体验证可以设置 `publish=false`、`cpkg_ref=<包仓库验证分支或提交>`：仍验证两个实际包及 OCI 元数据，不写入 GHCR，也不发布 GitHub Release。

GHCR 新包默认是 Private，需要包所有者首次将 Cirrus 包设置为 Public。正式发布会检查匿名拉取；被拒绝时明确失败。设置为 Public 后重跑失败的发布 job 即可使用原运行的包继续，不需要重建或修改版本内容。

需要先安装支持 OCI index 的 cpkg 版本，再使用：

```bash
cpkg -S cirrus
```

这只扩展 Cirrus 的双架构发布；其他云端包支持的架构以各包元数据为准。所有构建与发布：**该操作需要通过 GitHub Actions 执行。**
