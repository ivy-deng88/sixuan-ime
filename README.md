# 思玄输入法 · Sixuan IME

面向 Ubuntu GNOME 的中文输入客户端，使用 Go、C、IBus、Rime、GTK 3 和 PulseAudio 开发。

- 本地拼音输入、候选选择、中英文切换。
- 长按 Ctrl 开始语音输入；松手后继续录音，再按 Ctrl 或空格结束。
- 流式显示识别结果，录音悬浮窗、声音波形、提示音。
- 可选云候选、文字整理、外观配置。
- 支持思玄语音服务、FunASR 和 OpenAI 兼容的语音识别服务。

## 许可

本项目采用 [PolyForm Noncommercial 1.0.0](LICENSE.md)。允许符合该许可证的非商业使用、修改和分发，须保留许可和通知；商业使用需另行取得授权。这是非商业源码许可，不属于 OSI 定义的开源许可证。第三方组件仍适用各自的许可证，见 [第三方声明](THIRD_PARTY_NOTICES.md)。

## 构建与安装

当前验证环境为 Ubuntu 26.04 amd64、GNOME 50 / Wayland。其他发行版和桌面环境需要自行适配；候选窗扩展目前声明支持 GNOME 50。在 KDE Plasma 等非 GNOME 桌面上，`sixuan-fcitx5 setup` 会把“思玄拼音”加入 IBus 引擎列表，并打印候选面板（kimpanel）的配置步骤；候选窗皮肤仅 GNOME 支持。

需要 Go 1.25 或更新版本，以及系统开发依赖：

```sh
sudo apt install build-essential pkg-config libgtk-3-dev libibus-1.0-dev \
  librime-dev libpulse-dev libfcitx5core-dev rime-data-luna-pinyin ibus \
  ffmpeg dpkg-dev
make test
make build
make deb
sudo apt install ./dist/sixuan-fcitx5_1.2.5_amd64.deb
sixuan-fcitx5 setup
```

安装后注销并重新登录，在系统输入源中选择“思玄拼音”。设置入口为输入法菜单中的“思玄输入法设置”，也可运行 `sixuan-fcitx5 settings`。`setup` 会注册输入源、启用用户服务和候选窗扩展。

`make build` 只生成 `dist/sixuan-fcitx5`，不修改系统；桌面集成需要安装 deb 包。`make deb` 按当前主机架构打包，文件名随架构变化，并同时构建 Fcitx 5 插件（`make fcitx5` 只构建 `dist/libsixuan.so`，需要 `libfcitx5core-dev`）。

## Fcitx 5

思玄拼音也以 Fcitx 5 插件的形式提供，与 `fcitx5-rime` 等输入法并存，不替换系统里的 Fcitx 5。插件不直接链接 librime，Rime 仍由思玄守护进程持有，因此沿用同一套本地方案、词库和候选排序。

在 Fcitx 5 中启用：安装 deb 包后重启 Fcitx 5 或重新登录，然后在 Fcitx 5 配置工具（`fcitx5-config-qt`）的“输入法”列表中添加“思玄拼音”，也可以运行 `sixuan-fcitx5 setup --register-fcitx5` 自动添加（该操作会重启 Fcitx 5）。`sixuan-fcitx5 setup` 在检测到 Fcitx 5 会话时只打印以上指引，不会自行切换输入框架。

当前阶段 Fcitx 5 插件仅支持本地拼音输入；语音输入、云候选和候选窗皮肤只在 IBus 版本中提供。`pingyin.sh` 可用于在 IBus 与 Fcitx 5 会话之间手动切换（需要注销重新登录生效）。


## 语音服务

使用思玄服务时，在 [官网](https://sixuanai.com/) 账号页创建 API Key，然后通过标准输入保存，不把密钥写到命令参数中：

```sh
sixuan-fcitx5 asr config --provider sixuan --api-key-stdin
# 粘贴自己的 API Key，换行后按 Ctrl+D
sixuan-fcitx5 asr status
```

也可使用自己部署的 FunASR（以下为本机示例地址）：

```sh
sixuan-fcitx5 asr config --provider funasr --url ws://127.0.0.1:10095
```

OpenAI 兼容服务使用 `--provider openai --url <接口地址> --model <模型名称>`，按服务要求通过 `--api-key-stdin` 配置密钥。公开网络连接应使用 HTTPS/WSS。服务端独立部署，不包含在本仓库中；客户端源码许可不授予在线服务的免费额度。

## 云候选与隐私

```sh
sixuan-fcitx5 cloud status
sixuan-fcitx5 cloud enable
sixuan-fcitx5 cloud disable
```

云候选通过思玄服务提供，无需 API Key；启用后会将当前拼音发送到该服务。语音识别会将录音发送到你配置的识别服务。可以关闭云候选，只使用本地拼音。

配置位于 `$XDG_CONFIG_HOME/sixuan-ime`（默认 `~/.config/sixuan-ime`）。API Key 文件权限为 `0600`。不要向仓库或 Issue 上传配置、录音、识别内容和凭据。

## 开发

```sh
make test        # 单元测试，包括本地模拟的 HTTP/WebSocket 服务
make vet         # 静态检查
make build       # 独立构建，去除构建机路径
```

部分音频设备测试需要显式配置测试声源，默认跳过；自动测试不验证真实桌面交互或在线服务的可用性。

问题反馈请使用本仓库 Issues；安全问题处理见 [SECURITY.md](SECURITY.md)。
