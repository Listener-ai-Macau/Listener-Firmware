# Listener Firmware

Listener 語音鍵盤的韌體——真正跑在裝置上的那份程式碼。ESP32-S3、PDM 麥克風、一顆能按的旋鈕、四顆鍵、六盞燈、藍牙音訊、OTA 升級。

鍵盤只管聽和亮燈。把語音變成文字是電腦那邊的事,歸 [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) 管。

[English](README.md) · [简体中文](README.zh.md) · [版本說明](docs/release/1.0.5.md)

## 鍵盤怎麼用

- 單擊旋鈕:開始、停止聽寫。
- 雙擊:重新配對;長按:關機;轉動:調電腦音量(都能在 Type 裡改)。
- 四顆鍵隨你綁,在 Type 裡設定:貼上、複製、開啟應用,想綁什麼綁什麼。
- 燈會說話:電源、藍牙、錄音、處理各佔一盞。全黑多半是睡著了,不是壞了。
- 懶得伸手也行:說一句「開始錄音」,它自己開始。

第一次用:充電,按旋鈕開機,在 Type 裡發起配對,Windows 藍牙裡選 `listener`。完整步驟看[語音鍵盤手冊](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/quickstart/voice-keyboard-readme.md)。

## 升級

最省事是在 Listener Type 裡 OTA,設定都會保留。目前版本 1.0.5([版本說明](docs/release/1.0.5.md))。已知問題一個:剛關機後的電量讀數可能不準。

## 自己建構燒錄

腳本是 Windows 的,會幫你裝好 ESP-IDF v5.5:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

別直接跑 `idf.py`——用 `tools\idf.ps1`,它會先載入 IDF 環境。

有意思的部分都能讀:音訊鏈路、燈效邏輯、藍牙協議。GPIO 細節在 [docs/features/firmware-feature-map.md](docs/features/firmware-feature-map.md)。歡迎報 bug、提 PR——先翻翻 [CONTRIBUTING.md](CONTRIBUTING.md);安全問題發 [SECURITY.md](SECURITY.md)。
