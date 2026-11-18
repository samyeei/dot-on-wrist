# Dot on Wrist

[English](../README.md) · [简体中文](README.zh-CN.md)

![Dot on Wrist のプロジェクト画像](../media/hero.png)

M5Stack StopWatch または Apple Watch から Dot と話せます。Mac 上のゲートウェイが、ログイン済みの Chrome の会話タブに接続します。長押しで質問を送信し、通話ボタンで連続通話を開始できます。新着メッセージは画面に表示し、読み上げます。

## M5Stack StopWatch の使い方

- **左の黄色ボタン**を長押しして 1 件録音し、離すと送信します。Dot の返信は画面に表示され、読み上げられます。
- **右の青色ボタン**を押すと連続通話が始まり、もう一度押すと終了します。
- **待機中の新着**。Mac に接続している間は、Dot から新着が届くと画面が点灯し、メッセージの表示と読み上げができます。
- **音量、明るさ、自動消灯**。画面の上端から下にスワイプし、設定のスライダーでスピーカーの音量と画面の明るさを調整できます。初期設定では操作がないと 30 秒で消灯します。待機時の実測はおよそ 4〜5 時間で、それ以上持つ場合もありました。Wi-Fi の電波や明るさ、読み上げ頻度によって変わります。
- **常時点灯**。設定で有効にすると、画面が点灯したままになり、Dot が大きく表示されます。バッグに付けるアニメーション付きのチャームとしても使えます。
- **Dot と遊ぶ**。待機画面で Dot をタップすると反応します。通常表示では、なでたり、ドラッグしたり、指ではじいたりできます。

## Apple Watch の使い方

Dot を長押しして録音し、離して送信します。「通話」で連続通話を始め、「終了」で切ります。アプリを開いて待機中は、そのとき開いてから届いた新着を表示して読み上げます。

## エージェントにそのまま貼り付ける

Mac を操作できるエージェントに、以下をコピーして渡してください。

```text
この Mac に https://github.com/samyeei/dot-on-wrist の Dot on Wrist をセットアップしてください。最初に docs/INSTALL.md と既存環境を確認し、M5Stack StopWatch と Apple Watch のどちらを使うか確認してください。Python 環境を作成し、ローカルのペアリング、Chrome 拡張機能の読み込み、ゲートウェイの起動まで進めてください。会話 URL は私が非表示の入力欄に入力します。M5Stack はモデルと USB ポートを確認し、フラッシュ全体をバックアップしてから書き込んでください。Apple Watch は私の Xcode 開発チームで署名し、ペアリング済みの時計にインストールしてください。ログイン、権限許可、デベロッパーモード、機器の操作が必要な場合は私に知らせてください。最後に単発の音声返信と連続通話の終了を確認し、実際に確認できた結果を報告してください。秘密鍵、トークン、会話 URL、バックアップ、ペアリング済みビルドはローカルに保存してください。
```

## 手動インストール

1. Mac に Chrome、Python 3.12、BlackHole 2ch を用意し、自分の Dot 会話タブを開きます。セットアップスクリプトでローカルのペアリング情報を作成します。
2. Chrome の拡張機能ページでデベロッパーモードを有効にし、`mac/chrome-extension` を読み込みます。ゲートウェイを起動し、会話タブを開いたままにします。
3. M5Stack は PlatformIO でビルドし、USB で書き込みます。日本語版には `m5stack-stopwatch-ja` を選択します。初回は QR コードで 2.4 GHz Wi-Fi を設定します。
4. Apple Watch は `watchOS/ANNA.xcodeproj` を Xcode で開き、開発チームとペアリング済みの時計を選択して実行します。画面の言語はシステム設定に従います。


## リポジトリのルートで実行

`YOUR_MAC_LAN_IP` を Mac の LAN アドレスに置き換え、自分の ChatGPT Dot の会話 URL を入力します。Apple Watch は Xcode でインストールします。

```sh
python3.12 -m venv .venv
.venv/bin/python -m pip install -r mac/requirements.txt platformio "esptool>=4,<5"
.venv/bin/python scripts/configure.py --host YOUR_MAC_LAN_IP
./scripts/start-gateway.sh
```

[詳しい手順とコマンド](INSTALL.md)
