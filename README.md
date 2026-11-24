# Dot on Wrist

[简体中文](docs/README.zh-CN.md) · [日本語](docs/README.ja.md)

![Dot on Wrist for M5Stack StopWatch and Apple Watch](media/hero.png)

Talk to your ChatGPT Dot conversation from an M5Stack StopWatch or Apple Watch. A local Mac gateway connects both devices to your signed-in Chrome tab. Hold to ask one question, or start a live voice call. New incoming messages can appear as text and be read aloud.

## Install with an agent

Copy the text below into an agent that can operate your Mac.

```text
Set up Dot on Wrist from https://github.com/samyeei/dot-on-wrist on this Mac.
Read docs/INSTALL.md and inspect the existing setup first.
Ask me whether I want M5Stack StopWatch, Apple Watch, or both if that is not already clear.
Reuse compatible dependencies and install missing ones, create the Python environment, and run the repository's local configuration and gateway scripts.
Use my own Dot conversation URL through the hidden prompt.
Help load the Chrome extension and confirm its badge is ON.
For M5Stack, confirm the model and USB port, create and verify a local full-flash backup, build the firmware and flash the correct device.
For Apple Watch, help me use my Xcode development team, sign the app, and install it on my paired watch.
Pause for actions that require me to sign in, grant macOS permissions, use Developer Mode, or put the hardware into download mode.
Verify the connection, one short voice reply, and live-call hangup.
Report what was actually verified.
Keep conversation URLs, tokens, TLS keys, backups, and paired binaries local.
```

## M5Stack StopWatch

- **Left button (yellow)**. Hold to record one message and release to send it. Dot’s reply appears on screen and is read aloud.
- **Right button (blue)**. Press once to start a continuous voice call. Press again to hang up.
- **Incoming messages**. While the StopWatch is connected to the Mac and idle, new messages from Dot can wake the screen, appear as text, and be read aloud.
- **Screen and audio**. Swipe down from the top edge to open settings. Use the sliders to adjust speaker volume and screen brightness. The default display mode turns the screen off after 30 seconds of inactivity. In my standby testing, it lasted roughly 4–5 hours, sometimes longer. Wi-Fi signal, brightness, and audio use affect battery life.
- **Always-on mode**. Switch to **Always on** in settings to keep the display lit and make Dot larger. It works well as an animated bag charm.
- **Play with Dot**. On the idle screen, tap Dot for a reaction. Rub or drag the character, then flick it across the screen in the regular display mode.

## Apple Watch

Hold Dot to record and release to send. Tap **Call** to start a continuous call and **Hang Up** to end it. When the app is open and idle, it can display and read aloud messages received since you opened it.

## Network requirements

On the same local network, the watch connects directly to the Mac’s LAN address. **Away from home, you need a publicly reachable IP address for the Mac gateway** (or a hostname that resolves to it). The Mac must stay online with the ChatGPT Dot tab open. Connecting the watch to a phone hotspot gives it internet access, but the hotspot alone does not make your home Mac reachable. The default installer configures LAN access. For outdoor use, configure the public endpoint and TLS pairing for the device you use.

## Manual installation

| Component | Source | Installation |
| --- | --- | --- |
| M5Stack StopWatch | [firmware](firmware/) | Build and flash with PlatformIO by USB |
| Chrome extension | [mac/chrome-extension](mac/chrome-extension/) | Load unpacked and register the local native host |
| Apple Watch | [watchOS](watchOS/) | Build, sign, and run with Xcode |
| Mac gateway | [mac](mac/) | Set up the local Python environment and pairing |

[Manual installation guide](docs/INSTALL.md)

English is the default. Build the M5Stack firmware for Simplified Chinese or Japanese by choosing the corresponding PlatformIO environment. The Chrome extension and Apple Watch interface follow their system language. Conversation text comes from Dot. Some troubleshooting messages from the Mac gateway are still in Chinese.

This is an independent community project. The Chrome extension uses the current structure of the Dot page. You need an account with access to the conversation and its voice controls. The Mac must stay online and the tab must stay open. The initial setup targets a trusted local network.

See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY_NOTICES.md).

## Community

Scan the QR code to join the Telegram group.

<img src="media/telegram-qr.jpg" alt="Telegram QR code" width="220">
