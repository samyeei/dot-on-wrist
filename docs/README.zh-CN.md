# Dot on Wrist

[English](../README.md) · [日本語](README.ja.md)

![Dot on Wrist 项目封面](../media/hero.png)

在 M5Stack StopWatch 或 Apple Watch 上和 Dot 对话。Mac 网关连接已经登录的 Chrome 对话页面，支持按住说一句、连续通话以及新消息文字和语音提醒。

1. 在 Mac 上准备 Chrome、Python 3.12 和 BlackHole 2ch。打开你自己的 Dot 对话，用安装脚本在本机生成配对文件。
2. 打开 Chrome 的扩展页面，开启开发者模式并加载 `mac/chrome-extension`。启动 Mac 网关，保持对话标签页打开。
3. M5Stack 用 PlatformIO 构建和刷写，首次开机扫码连接 2.4 GHz Wi-Fi。中文构建选择 `m5stack-stopwatch-zh`。
4. Apple Watch 用 Xcode 打开 `watchOS/ANNA.xcodeproj`，选自己的开发团队和已配对手表，签名后运行。界面跟随系统语言。

M5Stack 按住黄键说话，松手发送，蓝键开始或结束连续通话。Apple Watch 按住 Dot 说话，底部按钮发起通话，中央按钮挂断。两端在连接后收到的新消息可在空闲时显示并朗读。手表和 Mac 需在能互相访问的局域网。

## 在仓库根目录执行

把 `YOUR_MAC_LAN_IP` 换成 Mac 的局域网地址。配对脚本会提示输入自己的 ChatGPT Dot 对话地址。Apple Watch 请通过 Xcode 安装。

```sh
python3.12 -m venv .venv
.venv/bin/python -m pip install -r mac/requirements.txt platformio "esptool>=4,<5"
.venv/bin/python scripts/configure.py --host YOUR_MAC_LAN_IP
./scripts/start-gateway.sh
```

[完整安装命令与排查](INSTALL.md) · [发给 Agent 的安装指令](AGENT-INSTALL.zh-CN.md)
