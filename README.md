# AirPlay Receiver for Windows — iOS Screen Mirroring (60fps, high-res)

> A Windows AirPlay receiver that lets an **iPhone/iPad mirror its screen to a PC with no app installed on the phone** — including watching **YouTube together** at **60fps** and **super‑sampled high resolution**.<br>
> 讓 **iPhone/iPad 不裝任何 app**，用內建「螢幕鏡像」把畫面投到 Windows PC 的 AirPlay 接收器 —— 可以**一起看 YouTube**，支援 **60fps** 與**超採樣高解析度**。<br>
> **Works across a wide range of iOS** — tested from **iOS 12.5.8 (iPhone 6)** to **iOS 26 & 27**, old and new iPhones alike.<br>
> **支援超廣 iOS 版本** —— 從 **iOS 12.5.8（iPhone 6）** 到 **iOS 26、27** 都實測可用，新舊 iPhone 通吃。

**This project is a fork of / built on [xenos1337/AirPlayServer](https://github.com/xenos1337/AirPlayServer) (MIT), which itself builds on [fingergit/airplay2-win](https://github.com/fingergit/airplay2-win). Huge thanks to both — their work is the foundation that made all of this possible.**

**本專案基於 [xenos1337/AirPlayServer](https://github.com/xenos1337/AirPlayServer)（MIT 授權）延伸開發，而它又源自 [fingergit/airplay2-win](https://github.com/fingergit/airplay2-win)。核心的 AirPlay 連線與解碼都來自它們，由衷感謝！**

> For personal / non‑commercial use. 僅供個人／非商業用途。

---

## Download / 下載

| File / 檔案 | Type / 類型 | When to use / 何時用 |
|---|---|---|
| [**`AirPlayReceiver.exe`**](https://github.com/YangMieh/AirPlayReceiver/releases/download/v1.0.1/AirPlayReceiver.exe) | Single file / 單一檔（建議 recommended） | Easiest — just double‑click. 最簡單，雙擊就跑。 |
| [**`AirPlayReceiver-v1.0.1-win-x64.zip`**](https://github.com/YangMieh/AirPlayReceiver/releases/download/v1.0.1/AirPlayReceiver-v1.0.1-win-x64.zip) | Folder / 資料夾 | If antivirus flags the single .exe (false positive). 若防毒誤報單一檔就改用這個。 |

The single `.exe` self‑extracts on first run (may trigger an antivirus false positive — use the `.zip` if so). Requires **Apple Bonjour** (free, one‑time): [support.apple.com/106380](https://support.apple.com/106380).

單一 `.exe` 首次執行會自解壓（可能被防毒誤報，遇到就改用 `.zip`）。需先裝免費的 **Apple Bonjour**：[support.apple.com/106380](https://support.apple.com/106380)。

---

## English

### What this fork adds
This fork is **rebased on upstream [v1.1.2](https://github.com/xenos1337/AirPlayServer/releases/tag/v1.1.2)**, which already gives you YouTube‑via‑mirroring, no video‑mode crash, and the modern dark UI. On top of that base, this fork adds:

- **60fps** — we patch the advertised display `maxFPS` (30 → 60) so the iPhone actually *streams* at 60fps (upstream's presets only pace the render side).
- **High‑resolution super‑sampling** — we bump the advertised display resolution so the iPhone sends a higher‑res mirror; downscaled into the window it looks noticeably crisper (like watching 4K content on a 1080p screen).
- **No green screen** — the SDL renderer prefers D3D9 / OpenGL and never D3D11, whose YUV path renders all‑green on some GPUs/drivers.
- **Hotkeys always work** — IME is disabled for the process so `H` / `F` work in any input language (no need to switch off Zhuyin/CJK input).

> Watching YouTube together via mirroring and surviving the AirPlay‑video crash used to be this fork's headline features. As of v1.0.1 the upstream project fixes both at the source level, so we now inherit them cleanly instead of patching at runtime.

### Requirements
- Windows 10/11 (x64)
- **Apple Bonjour** (for device discovery) — install it from Apple's official page:
  [**support.apple.com/106380**](https://support.apple.com/106380) (auto‑localizes to your region).
  *If it's missing, the app opens this link for you automatically on startup.*
- An iPhone/iPad on the same Wi‑Fi network

### Build
Open `AirPlay.sln` in Visual Studio 2022 (Desktop C++ workload), or from a *x64 Native Tools Command Prompt*:
```
MSBuild AirPlayServer\AirPlayServer.vcxproj /p:SolutionDir=<repo>\ /p:Configuration=Release /p:Platform=x64
```
Output: `x64\Release\AirPlayReceiver.exe` (the runtime DLLs live next to it).

### Usage
1. Run `AirPlayReceiver.exe`.
2. On the iPhone: Control Center → **Screen Mirroring** → pick the receiver.
3. For sharpest video, watch fullscreen in landscape.
4. Move the mouse to the **top edge** for the toolbar; press **H** for the info panel, **F** for fullscreen.

### Known limitations
- **Netflix / DRM content won't play** — this is a hardware/OS DRM wall (FairPlay), impossible for any homemade receiver. Watch Netflix directly on the PC instead.
- YouTube's on‑screen controls occasionally get stuck (rare; reopen to clear).
- Very high resolution uses a lot of bandwidth; on weak Wi‑Fi you may see occasional frame drops.

---

## 繁體中文

### 這個 fork 加了什麼
這個版本**改以上游 [v1.1.2](https://github.com/xenos1337/AirPlayServer/releases/tag/v1.1.2) 為基底** —— 用鏡像看 YouTube、不再閃退、現代深色介面，這些上游本身就有了。在這個基底上，本 fork 額外加上：

- **60fps** —— 攔改接收器宣告的 `maxFPS`（30 → 60），讓 iPhone 真的以 60fps *串流*（上游的預設只是在電腦端補幀）。
- **高解析超採樣** —— 把宣告的顯示解析度改大，iPhone 就送更高解析的鏡像；縮小塞進視窗後明顯更銳利（就像在 1080p 螢幕上看 4K 內容）。
- **消除綠畫面** —— SDL renderer 優先走 D3D9／OpenGL、絕不用 D3D11（它的 YUV 路徑在某些顯卡會整片綠）。
- **熱鍵永遠有效** —— 停用整個程式的 IME，`H`／`F` 在任何輸入法下都能按（不用關掉注音）。

> 「用鏡像一起看 YouTube」和「不再因 AirPlay 影片模式閃退」原本是這個 fork 的招牌；到了 v1.0.1，上游已在原始碼層級把這兩件事做好，所以我們現在是乾淨地繼承，而非執行期硬 patch。

### 需求
- Windows 10/11（x64）
- **Apple Bonjour**（裝置探索用）—— 從 Apple 官方頁面安裝：
  [**support.apple.com/106380**](https://support.apple.com/106380)（會自動導到你的語言）。
  *若沒裝，程式啟動時會自動幫你開這連結。*
- 與 PC 同一 Wi‑Fi 的 iPhone/iPad

### 建置
用 Visual Studio 2022（桌面 C++ 工作負載）開 `AirPlay.sln`，或在「x64 Native Tools 命令提示字元」執行：
```
MSBuild AirPlayServer\AirPlayServer.vcxproj /p:SolutionDir=<repo>\ /p:Configuration=Release /p:Platform=x64
```
輸出：`x64\Release\AirPlayReceiver.exe`（執行期 DLL 就在旁邊）。

### 使用
1. 執行 `AirPlayReceiver.exe`。
2. iPhone：控制中心 →「**螢幕鏡像**」→ 選這台接收器。
3. 想要最清晰的影片，把手機轉橫、影片全螢幕。
4. 滑鼠移到**視窗上緣**叫出工具列；按 **H** 開資訊面板、**F** 全螢幕。

### 已知限制
- **Netflix／DRM 內容無法播放** —— 這是硬體/系統層級的 DRM 硬牆（FairPlay），任何自製接收器都不可能繞過。請直接在 PC 上開 Netflix。
- YouTube 的畫面控制鈕偶爾會卡住（少見；重開即可清除）。
- 極高解析度很吃頻寬；Wi‑Fi 較弱時可能偶爾掉幀。

---

## Credits / 致謝
- **[xenos1337/AirPlayServer](https://github.com/xenos1337/AirPlayServer)** — the base receiver this project is built on. 本專案的直接基礎。
- **[fingergit/airplay2-win](https://github.com/fingergit/airplay2-win)** — the original project xenos built upon. 更上游的原始專案。
- Bundled open‑source libraries: SDL2, FFmpeg, Dear ImGui, Apple Bonjour (mDNSResponder), libplist, FDK‑AAC.

## License / 授權
MIT License — see [LICENSE](LICENSE). Original copyright © 2025 xenos1337 is retained.
MIT 授權 —— 見 [LICENSE](LICENSE)，保留原作者 xenos1337 的版權聲明。
