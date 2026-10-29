# Codex Computer Use 截图修复技能

适用于 Windows 10 x64 Codex Computer Use 截图故障的可自动发现技能。出现截图超时或边框接口错误时，技能指导 Codex 诊断、构建兼容层、验证并修复当前 helper。

将 `skills/codex-computer-use-repair` 复制到 Codex 的 skills 目录后重启 Codex 以刷新技能发现。也可显式调用 `$codex-computer-use-repair`。自动发现由 Codex 根据请求决定，不是后台守护程序。

需要 Visual Studio C++ x64 工具、MASM、Windows SDK 和可用的交互桌面。兼容层是未签名 DLL，具体适用条件与卸载方式请参阅上游文档。请仅针对当前实际 helper 安装。

## 原作者与来源

本技能封装 [MagicalAstrogy/CodexComputerUseFix](https://github.com/MagicalAstrogy/CodexComputerUseFix)，原作者 MagicalAstrogy；内含上游提交 `1a56b5578d86932b43f958a084253ee00081b049` 的源码和原始 [WTFPL 许可证](skills/codex-computer-use-repair/upstream/LICENSE)。技能说明由本仓库新增，与上游项目无隶属关系。
