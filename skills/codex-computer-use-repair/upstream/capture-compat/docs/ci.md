# CI 与 Release

[简体中文](ci.md) | [English](ci_en.md)

工作流位于 [build.yml](../../.github/workflows/build.yml)，打包逻辑位于 [package.ps1](../package.ps1)。

## 触发方式

| 操作 | 构建与测试 | Actions 产物 | GitHub Release |
| --- | --- | --- | --- |
| 推送 `main` | 普通版与 Trace 版 | 保留 14 天 | 不创建 |
| 向 `main` 提交 PR | 普通版与 Trace 版 | 保留 14 天 | 不创建 |
| 手动运行工作流 | 普通版与 Trace 版 | 保留 14 天 | 不创建 |
| 推送 `v1.0.0` 等版本标签 | 普通版与 Trace 版 | 保留 14 天 | 两个构建通过后发布 |

版本标签使用 `v主版本.次版本.修订号`，可添加 `-rc.1`、`-beta.1` 等预发布后缀。带后缀的 Release 标记为 prerelease，并且不设为 Latest。标签校验失败会停止构建；工作流不会自行创建 Git 标签。

普通分支构建使用 `ci-<运行编号>-<提交短哈希>` 作为包版本。标签构建使用完整标签名。

## 构建和测试

构建任务使用 `windows-2022` 和镜像中提供的 MSVC、MASM、Windows SDK。普通版与 Trace 版分别在独立 runner 中运行：

1. `build.ps1` 编译 x64 DLL、探针和测试窗口，并执行 COM 与派发单元测试。
2. `tests/install_tests.ps1` 使用模拟 helper 测试安装、卸载和文件保护。
3. `package.ps1` 生成发布 ZIP 及 SHA-256 校验文件。
4. `tests/package_tests.ps1` 验证压缩包哈希、二进制与构建输出一致、目录结构和文档链接，然后从解压目录再次执行安装测试。
5. 上传经过检查的 ZIP 和校验文件作为 Actions 产物。

CI 不执行 `validate.ps1`。该脚本依赖 Windows 10 的原生接口基线和交互桌面，托管的 Windows Server 构建环境不能替代真实截图回归。正式发布前按 [验证指南](validation.md)完成目标 Windows 10 环境测试。

工作流默认只有仓库读取权限，只有标签发布任务获得 `contents: write`。发布使用 GitHub 提供的 `GITHUB_TOKEN`。Actions 依赖固定到提交 SHA，升级时应同时核对版本注释和提交值。

## 下载与安装

正式版本从 [Releases](https://github.com/MagicalAstrogy/CodexComputerUseFix/releases) 下载。日常使用普通 ZIP，排查问题时使用带 `-trace` 后缀的 ZIP。

每个版本提供四个文件，以 `v1.0.0` 为例：

```text
CodexCaptureCompat-v1.0.0-windows-x64.zip
CodexCaptureCompat-v1.0.0-windows-x64.zip.sha256
CodexCaptureCompat-v1.0.0-windows-x64-trace.zip
CodexCaptureCompat-v1.0.0-windows-x64-trace.zip.sha256
```

Actions 页面也提供 `packages-windows-x64-release` 和 `packages-windows-x64-trace`。下载 Actions 产物后，先解开 GitHub 的外层压缩包，再使用其中的项目 ZIP 和校验文件。

将 ZIP 和对应 `.sha256` 放在同一目录，用 PowerShell 校验：

```powershell
$archive = '.\CodexCaptureCompat-v1.0.0-windows-x64.zip'
$expected = ((Get-Content -LiteralPath "$archive.sha256" -Raw).Trim() -split '\s+', 2)[0]
$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
if ($actual -ine $expected) { throw 'SHA-256 mismatch.' }
Expand-Archive -LiteralPath $archive -DestinationPath .\CodexCaptureCompat-v1.0.0
```

解压目录保留源码布局：根目录为中英文 README 和 LICENSE，`capture-compat/dist/` 为三个二进制文件，同级保留安装脚本、源码、测试和文档。进入 `capture-compat`，按 [安装指南](../README.md)部署 DLL。普通包与 Trace 包分别解压，避免混用文件。

## 发布一个版本

先把工作流和要发布的源码提交推送到 `main`，确认该提交的 CI 通过。在对应提交上创建并推送一个尚未使用的版本标签：

```powershell
# 示例版本号，发布时替换为实际版本。
git tag -a v1.0.0 -m 'Release v1.0.0'
git push origin v1.0.0
```

标签触发重新构建。发布任务下载两个构建的产物，复核 SHA-256，再使用 `gh release create --verify-tag --generate-notes` 创建 Release 并上传四个文件。发布说明由 GitHub 自动生成。

只有标签的 push 事件会发布；手动选择标签运行工作流仍只生成 Actions 产物。发布前失败时可在 Actions 页面重跑工作流。若该标签已经有 Release，创建操作会报错，不覆盖现有发布或附件；更新内容应使用新版本标签。

## 本地打包

在 `capture-compat` 目录中执行：

```powershell
# 普通版：先构建，再打包和验证。
.\build.ps1
$package = .\package.ps1 -Version v1.0.0
.\tests\package_tests.ps1 -ArchivePath $package.archive

# 诊断版必须使用对应的 -Trace 构建。
.\build.ps1 -Trace
$tracePackage = .\package.ps1 -Version v1.0.0 -Trace
.\tests\package_tests.ps1 -ArchivePath $tracePackage.archive
```

`package.ps1` 打包现有的 `dist/`，不会重新编译。`-Trace` 选择诊断包命名，必须与刚运行的构建选项一致。两种配置共用 `dist/`，因此要按“构建、打包、验证”的顺序分别处理；重新构建会替换其中的文件。

| 参数 | 默认值 | 作用 |
| --- | --- | --- |
| `-Version` | `dev` | 包名中的版本；只接受字母、数字、点、加号和连字符，以字母或数字开头 |
| `-Trace` | 不启用 | 使用 `-trace` 包名后缀 |
| `-OutputDirectory` | 仓库根目录 `artifacts/` | ZIP 和校验文件的输出位置 |

同名 ZIP 或校验文件已经存在时，打包会拒绝覆盖；使用新版本号或新的输出目录。临时打包目录在结束时清理，安装和解压测试的夹具保留在 `validation/` 中。包内不包含编译中间文件、验证记录、截图、Git 历史或迁移副本。
