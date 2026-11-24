# CrossDesk fork 改动记录

本 fork 为 [fortruelove/crossdesk](https://github.com/fortruelove/crossdesk)，维护分支为 `codex/self-hosted-signaling-default`。桌面版 `v1.7.7.2` 基于原作者的 [v1.7.7-20261124](https://github.com/kunkundi/crossdesk/releases/tag/v1.7.7-20261124)，上游提交为 `c3916910653a093588c8a85ca3b6f886720edd49`。

## 需要随上游版本保留的改动

| 改动 | 行为和主要代码 |
| --- | --- |
| 远控标题栏 | 多显示器时可直接点标签切换。连接信息支持悬停查看、点击固定、再次点击或点击外部关闭。见 `87e150ce`、`apps/desktop/src/gui/ui/stream_window.slint`、`gui_application.cpp` 和 `slint_stream_render_test.cpp`。 |
| Windows 自定义老板键 | 设置里可录入两个相同或不同的按键，按同一组合隐藏或恢复控制方本机窗口。见 `7f5990e7` 及 `boss_key_gesture_test`。 |
| Windows 便携版服务兼容 | 可复用已正确注册且仍有效的安装版锁屏控制服务，避免启动时误要求修复服务路径。见 `8574ee4d` 及 `windows_service_lifetime_test`。 |
| Intel 核显窗口兼容 | Windows 启动探测在实际 OpenGL 渲染器为可识别的 Intel 核显时选择软件渲染，修复在实际 UHD 630 操作机验证过的标题栏裁切和点击错位。规则按显卡名称识别系列，并将名称不明确的 Arc 交给默认探测。见 `apps/desktop/src/platform/windows/gui/slint_backend.*`、`slint_renderer_probe.cpp` 和 `slint_stream_render_test.cpp`。 |
| GitHub 更新和一键安装 | 桌面客户端只检查本 fork 的正式 Release。Windows 安装版验证 Ed25519 签名、安装包大小与 SHA-256，复用上游升级器执行安装和重启。首次需手动安装含发布公钥的版本。见 `version_checker.cpp`、`updater/update_core.cpp`、`.github/scripts/sign_fork_windows_update.py` 和 [更新说明](UPDATER.md)。 |
| Windows 双包发布 | 标签发布同时生成正式安装 EXE 和便携 ZIP。由发布流程为最终安装包生成签名，并把签名附在 GitHub 发布说明末尾。见 `c74f8760`、`.github/workflows/build.yml` 中的 `release-fork-windows`。 |
| iOS 自托管设置保存 | 保存后立即持久化，并处理旧设置迁移和进入后台时的保存。上游 1.7.7 也包含相关修复，合并后检查保存、退出、重开仍保留设置。见 `bd40cd69` 和 `apps/ios/CrossDeskMobile/RemoteSessionModel.swift`。 |

桌面和移动端的默认信令为上游 `api.crossdesk.cn:9099`。用户自行配置的服务器保存于客户端本地。早期 fork 的私有默认地址已回退，合并时不要把历史默认值带回安装包。

## 合并新版上游时

1. 以要合并的上游正式标签为基准，记录标签和提交，核对上表中已有的上游实现与 fork 差异。
2. 优先检查 `stream_window.slint`、`gui_application.cpp`、Windows 渲染器探测、升级器、发布工作流和 iOS 设置保存这些交叉修改点。
3. 运行标题栏、老板键、Windows 服务、更新检查和签名校验回归测试。Windows 包需分别检查安装版和便携版。在有条件时用 Intel 核显实机确认普通窗口的显示和点击位置。
4. 发布前核对新旧客户端使用的 fork 更新公钥一致，签名绑定最终安装 EXE。仓库 Actions 中使用 `CROSSDESK_UPDATE_PUBLIC_KEY` 变量和 `CROSSDESK_UPDATE_PRIVATE_KEY` 密钥。升级器只接受版本号更高的包。
5. 更新本文件的上游基线、功能清单和回归要点。移动端使用其独立版本号。
