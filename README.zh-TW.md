<h1 align="center">Listener Firmware</h1>

<p align="center">
  <strong>Listener 語音鍵盤裡的開源韌體。</strong><br/>
  按一下,說話,看燈——字出現在你的電腦上。
</p>

<p align="center">
  <a href="README.md">English</a> ·
  <a href="README.zh-CN.md">简体中文</a> ·
  <strong>繁體中文</strong>
</p>

<p align="center">
  <a href="https://github.com/Listener-ai-Macau/Listener-Firmware/releases"><img src="https://img.shields.io/github/v/release/Listener-ai-Macau/Listener-Firmware" alt="Release" /></a>
  <img src="https://img.shields.io/badge/ESP--IDF-v5.5-blue" alt="ESP-IDF v5.5" />
</p>

<p align="center">
  <a href="docs/product/features.md">產品功能</a> ·
  <a href="docs/release/1.0.5.md">版本說明</a> ·
  <a href="https://github.com/Listener-ai-Macau/Listener-Type">Listener Type</a>
</p>

<!-- 頭圖:有鍵盤照片後,放在這裡。 -->

這是你買回去那台鍵盤裡跑的軟體,不是 ESP32 範例工程。ESP32-S3、PDM 麥克風、EC11 旋鈕、四顆鍵、六盞狀態燈、藍牙音訊、低功耗、OTA 升級,都在這個倉庫裡。

聽見你、亮燈、把音訊送出去,是這個倉庫的事;把語音變成游標處的文字,是桌面軟體 [Listener Type](https://github.com/Listener-ai-Macau/Listener-Type) 的事。

## 裡面的硬體

- ESP32-S3(16 MB 快閃記憶體,8 MB PSRAM)
- PDM 數位麥克風,16 kHz 採音
- EC11 旋轉編碼器、四顆熱鍵、六燈狀態條
- 鋰電池,USB-C 充電

## 手上能做什麼

- **單擊旋鈕** —— 開始 / 停止聽寫
- **雙擊** —— 忘掉目前配對,重新可被搜尋
- **長按** —— 關機;**旋轉** —— 調電腦音量(可在 Type 裡改)
- **四顆鍵** —— 在 Type 裡幫單擊、雙擊、長按各綁一個動作:貼上、複製、開啟應用
- **六盞燈** —— PWR 電源、BLE 連線、REC 採音、AI 處理、OK、WARN。進行到哪一步一眼看清;燈滅了多半是睡著了,不是壞了
- **喚醒詞** —— 說一句「開始錄音」就開工;聲紋可在 Type 裡錄入,可選

## 30 秒出聲

1. 充電,單擊旋鈕開機。
2. 開啟 Listener Type 開始配對,在 Windows 藍牙裡選 `listener`。
3. 游標點進輸入框,單擊旋鈕,說話,再單擊。
4. REC 燈亮,Type 的膠囊有反應,字落在游標處。

手冊在 Type 倉庫:[語音鍵盤手冊](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/quickstart/voice-keyboard-readme.md)

## 誰幹什麼

| 鍵盤(本倉庫) | Listener Type(桌面軟體) |
| --- | --- |
| 負責聽見你:麥克風、鍵、燈、配對、電池、OTA | 負責替你寫:轉寫、整理、插入游標 |
| 沒有 Type,它是個守規矩的藍牙裝置 | 沒有鍵盤,Type 用電腦麥克風照樣工作 |

## 升級

在 Listener Type 裡 OTA——推薦,設定都會保留;或者用本倉庫的腳本走 USB 燒錄。目前版本 **1.0.5**([版本說明](docs/release/1.0.5.md))。已知問題:剛關機後的電量讀數可能不準。

## 建構和燒錄

腳本會幫你在 Windows 上裝好 ESP-IDF `release/v5.5`:

```powershell
pwsh -NoProfile -File .\tools\setup_windows.ps1
pwsh -NoProfile -File .\tools\build.ps1
pwsh -NoProfile -File .\tools\flash.ps1 -Port COMx
```

不要直接裸跑 `idf.py`——用 `tools\idf.ps1`,它會先載入 IDF 環境。

協議、燈效、音訊鏈路全部公開:歡迎閱讀、提 issue、發 PR。GPIO 表和除錯腳本在[韌體功能地圖](docs/features/firmware-feature-map.md);提交程式碼前請讀 [CONTRIBUTING.md](CONTRIBUTING.md),安全問題請走 [SECURITY.md](SECURITY.md)。
