# ZMK config — LiNEA40

LiNEA40 分割キーボードのZMKファーム設定リポジトリ。

- リモート: https://github.com/flutex/zmk-config-LiNEA40
- 作業ブランチ: `マウス動作安定版`
- ハード: 左右とも seeeduino_xiao_ble（右=central + PMW3610トラックボール、左=peripheral + EC11エンコーダー）

## 詳細リファレンス（最初に読む）

Obsidian:
- `MyVault/Projects/zmk-linea40.md` — west.yml 依存・実装済み機能・カスタムモジュール一覧
- `MyVault/Knowledge/zmk-v02-pointing-config.md` — ZMK v0.2 のpointer設定
- `MyVault/Knowledge/zmk-mouse-gesture-layer.md` — ジェスチャレイヤー
- `MyVault/Knowledge/zmk-battery-type-behavior.md` — バッテリー残量タイプ behavior

## ビルド方法

**通常改修（keymap・conf調整等）はローカルビルド禁止**。`docker` も `Makefile` も使わない。

正しい手順：
```bash
git add -A && git commit -m "..."
git push
# → GitHub Actions が自動でビルド → Actions の Artifacts から uf2 をダウンロード
```

過去にローカル（Docker/Makefile）で試みて失敗した実績あり（Obsidian: `Knowledge/mistakes.md` 2026-05-20）。

**適用除外（2026-07-03 ユーザー指示）**: カスタムモジュール開発（Mac設定アプリ用のストリーム通知モジュール等）で高速イテレーションが必要な場合は、ローカルwest環境などより有用な開発方法を使ってよい。推奨セットアップは Obsidian: `Decisions/2026-07-03-linea40-mac-app-architecture.md` と関連Knowledgeノートを参照。

## ZMK バージョン

- `west.yml` の zmk revision: **v0.2.1**
- v0.1 と API が違う箇所が多い（pointing/mouse 周り）。Obsidian の `zmk-v02-pointing-config.md` を確認してから書く

## カスタムモジュール（`app/` 配下）

```
zephyr/module.yml
app/Kconfig
app/CMakeLists.txt
app/src/behavior_os_layer.c           # OSレイヤー自動切替
app/src/behavior_battery_type.c       # バッテリー残量タイプ
dts/bindings/zmk,behavior-battery-type.yaml
```

## ⚠️ キーマップの二重管理に注意（2026-07-04〜）

Mac アプリ **LineaStudio**（`~/Projects/app/LineaStudio`）から ZMK Studio RPC で
キーマップをランタイム編集・保存できるようになった。

- Studio で「保存」した内容は**キーボードの flash（settings領域）に保存され、
  このリポジトリの keymap ファイルより優先される**
- Studio 保存後に `config/LiNEA40.keymap` を編集してファームを焼いても、
  **その変更は反映されない**（flash 側が勝つ）
- リポジトリの keymap を正に戻すには: 設定リセット（reset_settings RPC または
  `settings_reset` uf2 → 本体 uf2 焼き直し → BLE 再ペアリング）が必要
- どちらを正とするかは作業前にユーザーに確認すること

## 実装済み機能

- BLEプロファイル連動のOSレイヤー切替（Profile 0/3/4=Win、1=MAC、2=iOS）
- トラックボールジェスチャー（layer10_Gesture 有効時）
- WIRELESSレイヤー(6) Hキーでバッテリー残量タイプ入力（`L:XX% R:YY% BTn`）
