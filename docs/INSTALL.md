# Install Dot on Wrist

## Network access away from home

The walkthrough below pairs the watch for use on your local network. If you want to use it outdoors or from another network, you need an internet-reachable public IP for the Mac gateway, or a hostname pointing to that IP. Configure the public endpoint and TLS pairing for the device before leaving home. Keep the Mac online, keep Chrome open to ChatGPT Dot, and keep the gateway’s authentication and TLS enabled. A phone hotspot alone only gives the watch internet access.

## 1. Prepare the Mac

Use macOS 14.2 or newer, Chrome, Python 3.12, and FFmpeg on your PATH for Whisper. For Apple Watch, use Xcode 16 or newer with support for the installed watchOS version. Install [BlackHole 2ch](https://existential.audio/blackhole/) for live calls, then set the Dot tab's Chrome microphone to **BlackHole 2ch**. Leave macOS audio output on your usual speakers or headphones. Grant Chrome microphone access and grant the gateway's host application system audio recording access when macOS asks.

Open your own Dot conversation in Chrome. Keep that exact tab open. In a terminal from the repository root, run:

```sh
python3.12 -m venv .venv
.venv/bin/python -m pip install -r mac/requirements.txt platformio "esptool>=4,<5"
.venv/bin/python scripts/configure.py --host YOUR_MAC_LAN_IP
```

Use the Mac's actual LAN IPv4 address, preferably one reserved by your router. Paste the exact conversation URL into the hidden prompt. Setup generates a local CA, a TLS certificate, separate pairing credentials and the Chrome native host registration. Generated files stay local. If the LAN address changes, rerun configuration, reflash the M5Stack firmware and reinstall the watchOS app with the new pairing resource.

## 2. Load Chrome extension and start the gateway

1. Open `chrome://extensions` in Chrome and turn on **Developer mode**.
2. Choose **Load unpacked** and select `mac/chrome-extension`. Reload the extension if it was already loaded.
3. Keep the Dot tab open and start the gateway from the repository root with `./scripts/start-gateway.sh`. Leave the terminal running. The extension badge shows **ON** when the target tab is available.

The Mac listens on TCP ports 8765 (M5Stack) and 8767 (Apple Watch). Permit incoming connections on your trusted LAN if prompted. No router port forwarding is needed. Download the multilingual Whisper model before your first voice request:

```sh
.venv/bin/python -c "import whisper; whisper.load_model('base', device='cpu')"
```

Whisper detects the language of each single recording. For very short recordings, you can start the gateway with `DOT_TRANSCRIPTION_LANGUAGE=ja ./scripts/start-gateway.sh` to explicitly choose Japanese. Use `en` for English or `zh` for Chinese. For local speech playback, install the system voices Samantha, Kyoko, and Tingting as needed.

## 3. Install on M5Stack StopWatch

Use the supported ESP32-S3 M5Stack StopWatch (C152, 16 MB flash). Before overwriting firmware, back up the full flash and keep the backup locally. In download mode, identify its USB serial port. Where needed, check the command syntax with `python -m esptool --help` for your installed esptool version. The following commands use esptool v4:

```sh
.venv/bin/pio run -d firmware -e m5stack-stopwatch
.venv/bin/python -m esptool --chip esp32s3 --port YOUR_USB_PORT read_flash 0 0x1000000 stopwatch-backup.bin
.venv/bin/pio run -d firmware -e m5stack-stopwatch -t upload --upload-port YOUR_USB_PORT
```

Replace the environment with `m5stack-stopwatch-zh` or `m5stack-stopwatch-ja` for Chinese or Japanese. Restart the device, scan its Wi-Fi setup QR code and join **Dot-Setup**. If the setup page does not appear, visit `http://192.168.4.1` while connected to that setup network. Select a 2.4 GHz SSID on the Mac's LAN. Hold both physical buttons for about three seconds to reopen setup.

Hold the yellow button to record and release to send. Press the blue button for a live call and again to hang up. Unsolicited replies received after the device connects can appear and play aloud when it is idle.

## 4. Install on Apple Watch

Use watchOS 11 or later. Open `watchOS/ANNA.xcodeproj` in Xcode. Choose your development team in **Signing & Capabilities**, use a unique bundle identifier if required, then select your paired Apple Watch and click **Run**. Enable [Developer Mode](https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device) if Xcode requests it. Allow microphone and local network access if prompted.

On the watch, the green Mac dot shows that the gateway is reachable. Hold Dot to speak and release to send. Tap **Call** at the bottom for live conversation and **Hang Up** to end it. Incoming messages received after opening the app appear and are read aloud while the app is in the foreground and idle.

## Verify

Send one short request and confirm that a reply appears and is audible. Start and end a live call, then confirm that the microphone is released. Test an incoming message after the device is connected. If you are troubleshooting, check the target Chrome tab, BlackHole microphone selection, the Mac's LAN address and the app's connection indicator before reflashing or re-pairing.

Run the gateway tests locally with:

```sh
PYTHONPATH=mac .venv/bin/python -m unittest discover -s tests
```
