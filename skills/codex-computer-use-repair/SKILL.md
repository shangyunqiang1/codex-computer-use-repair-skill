---
name: codex-computer-use-repair
description: Diagnose and repair Windows Codex Computer Use screenshot failures, including FrameArrived timeouts and SetIsBorderRequired 0x80004002. Use when Computer Use window capture fails, not for unrelated screenshot apps.
---

# Codex Computer Use 截图修复

适用于 Windows 10 x64 官方 Computer Use helper 的 WGC 截图问题。原项目为 MagicalAstrogy 的 https://github.com/MagicalAstrogy/CodexComputerUseFix，源码和许可证位于 `upstream/`。

1. 读取当前可用的 computer-use 技能说明，使用其官方接口选择真实测试窗口并复现问题。记录确切错误；区分无窗口、锁屏、权限、浏览器路由、边框接口失败和 FrameArrived 超时。不可因超时就断言原因。用户取消操作或桌面锁定时停止。
2. 确认 Windows 和 x64 环境、交互桌面以及当前实际运行的 `codex-computer-use.exe` 路径。若存在多个不同路径，先确定本次调用使用的 helper；不能确定则停止。更新 Codex 后路径可能改变。保留已有配置、会话及未知 DLL。
3. 在本技能的 `upstream/capture-compat` 执行 `build.ps1`、`tests/install_tests.ps1`、`validate.ps1`。检查所有结果而非只看报告存在。WGC 基线针对 Windows 10；不适用环境或失败时停止并报告。需要 MSVC x64、MASM 和 Windows SDK。
4. 确认目标目录现有 DLL 和安装记录：若同哈希且记录匹配则保留并检查已加载情况；未知、不同或缺失记录的 DLL 不覆盖、不删除。用 `install.ps1 -HelperPath <实际路径> -WhatIf` 预览。仅在用户请求修复且权限允许时停止该路径下的 helper，执行安装并重启官方接口。脚本支持按记录校验卸载；不修改 helper 或系统 DLL。若权限被拒绝，说明障碍，不绕过权限。
5. 使用官方 computer-use 接口对 `dist/capture_test_window.exe` 进行真实截图，验证画面、连续截图、窗口尺寸变化及可访问性；确认 helper 加载了目标目录 DLL，核对 helper 与 DLL 哈希。关闭自己启动的测试窗口。报告故障、安装位置、每项验证和未通过事项。截图探针成功不能代替官方接口验证。

修复需要的代码与测试均随技能附带。保留上游 LICENSE；发布或修改时注明来源。自动发现意味着在相关故障时由 Codex 判断并执行，并非后台常驻监控或保证适配未来版本。
