# foo_monthly_stats

[![Build Status](https://github.com/shirafukayayoi/foo_monthly_stats/actions/workflows/build.yml/badge.svg)](https://github.com/shirafukayayoi/foo_monthly_stats/actions)

A foobar2000 component that tracks and visualizes your music listening statistics on a monthly and yearly basis.

![Monthly Stats Example](./image/sample.png)

![1772687992085](image/1772687992085.png)

![Monthly Stats Example](./image/image.png)

## Features

- 📊 **Monthly & Yearly Statistics**: View your listening history organized by month or year
- 📅 **Daily Statistics**: Track listening statistics by day alongside monthly and yearly views
- 🎵 **Track Play Counts**: Records the number of times each track is played with accurate playback time tracking
- 📈 **Month-over-Month Comparison**: See how your listening habits change with delta indicators
- 🎨 **Beautiful HTML Reports**: Export visually appealing reports with album artwork
- 📱 **Smartphone HTML Reports**: Export format optimized for mobile devices and social media (Instagram Stories 1080x1980px with rank numbering)
- 🎯 **Rank Numbering**: Visual rank badges (#1, #2, #3) for top artists and tracks in smartphone format
- 🏆 **Top Artists Ranking**: Highlights your most-played artists with circular album art
- ⏱️ **Total Listening Time**: Calculates and displays your total listening time
- 🗄️ **SQLite Database**: Efficient local storage of listening history
- 🖼️ **Album Art Integration**: Displays album artwork in reports using embedded thumbnails
- ❌ **Selective Entry Removal**: Remove specific tracks from monthly statistics while preserving original playback history

## Installation

### Option 1: Install Pre-built Component

1. Download the latest `.fb2k-component` file:
   - **Stable releases**: [Releases](../../releases) page
   - **Latest builds**: [Actions](../../actions) tab → Select latest workflow → Download from Artifacts section (note: files are in zip format)
2. If downloaded from Actions, extract the zip file to get the `.fb2k-component` file
3. Open foobar2000
4. Go to **File → Preferences → Components → Install...**
5. Select the `.fb2k-component` file
6. Restart foobar2000

### Option 2: Build from Source

See [BUILD.md](BUILD.md) for detailed build instructions.

**Note**: This project uses GitHub Actions for automated builds. You can get pre-built binaries from the Actions tab without setting up a local build environment.

Local build requirements:

- Visual Studio 2022 or later (with C++ desktop development and ATL/MFC components)
- Windows 10/11
- foobar2000 SDK (included in repository)

## Usage

### Opening the Dashboard

1. In foobar2000, go to **View → Monthly Stats Dashboard**
2. The dashboard window displays your current month's statistics by default

### Navigating Statistics

- **◀ / ▶ buttons**: Navigate to previous/next month, year, or day (depending on selected view)
- **Day/Month/Year toggle**: Cycle through Daily → Monthly → Yearly views
- **Day navigation**: Switch to Day view mode and navigate between days using Previous/Next arrows
- **Reset button**: Reload statistics from the database
- **Export button**: Generate HTML report with your statistics
- **Export format**: Use "Export: format" button to toggle between Desktop and Smartphone (mobile-optimized) formats
- **Remove Selected**: Select tracks to delete and click "Remove Selected" to remove from current period (original history preserved in database)

### Viewing Reports

Exported HTML reports include:

- Title with period (e.g., "Monthly Stats – 2026-02" or "Yearly Stats – 2026")
- Total listening time summary
- Top Artists ranking with album artwork
- Complete track listing with play counts and comparison to previous period
- Beautiful gradient design optimized for sharing

## How It Works

- **Playback Tracking**: The component uses foobar2000's `play_callback` API to monitor playback events
- **Accurate Time Recording**: Records actual playback time (stops counting if you skip or pause)
- **Background Processing**: Database operations run on a separate worker thread to avoid UI blocking
- **Album Art Collection**: Extracts and converts album art to base64 JPEG for embedding in HTML reports

## Technical Details

- **Language**: C++17
- **UI Framework**: WTL (Windows Template Library) + ATL
- **Database**: SQLite 3 (amalgamation)
- **XML Processing**: pugixml
- **SDK**: foobar2000 SDK 2023
- **Build System**: MSBuild with custom Directory.Build.props/targets

## Database Schema

The component stores data in `monthly_stats.db` located in your foobar2000 profile directory.

**Main Table: `monthly_count`**

- `ym`: Year-Month (e.g., "2026-02")
- `track_crc`: CRC32 hash of normalized track path
- `path`: Original file path
- `title`, `artist`, `album`: Metadata fields
- `playcount`: Number of times played
- `length_seconds`: Total playback time in seconds

## License

This project includes code and libraries under various licenses:

- **foobar2000 SDK**: See [sdk-license.txt](../sdk-license.txt)
- **SQLite**: Public Domain
- **pugixml**: MIT License
- **WTL**: Microsoft Public License
- **Catch2** (tests): Boost Software License 1.0

The plugin code itself is released under the MIT License.

## Contributing

Contributions are welcome! Please ensure:

- Code follows the existing style conventions
- Changes are tested on x64 Release builds
- Database schema changes include migration logic

## Acknowledgments

- foobar2000 and its SDK by Peter Pawlowski
- SQLite by D. Richard Hipp
- pugixml by Arseny Kapoulkine
- WTL by Microsoft

---

# 日本語説明

foobar2000用の月次・年次再生統計コンポーネントです。

## 主な機能

- 📊 **月次・年次統計**: 月別または年別に再生履歴を表示
- 📅 **日次統計**: 月次・年次表示に加えて日別リスニング統計を追跡
- 🎵 **トラック再生回数記録**: 正確な再生時間トラッキングで各曲の再生回数を記録
- 📈 **前月比較**: 前月/前年との比較をデルタ表示で確認
- 🎨 **美しいHTMLレポート**: アルバムアートワーク付きの見やすいレポートをエクスポート
- 📱 **スマートフォン向けHTMLレポート**: モバイルデバイスとソーシャルメディア向けに最適化（Instagram Stories 1080x1980px、ランク番号表示付き）
- 🎯 **ランク番号表示**: スマートフォン形式でトップアーティストとトラックに視覚的なランクバッジ（#1、#2、#3）を表示
- 🏆 **トップアーティストランキング**: 最も再生したアーティストを丸いアルバムアートで表示
- ⏱️ **総再生時間**: 累計リスニング時間を計算・表示
- 🗄️ **SQLiteデータベース**: 効率的なローカルストレージ
- 🖼️ **アルバムアート統合**: レポートにアルバムアートワークをサムネイル埋め込み
- ❌ **個別エントリー削除**: 月次統計から特定のトラックを削除（元の再生履歴はデータベースに保持）

## インストール方法

### 方法1: ビルド済みコンポーネントをインストール

1. [Releases](../../releases)ページから最新の`.fb2k-component`ファイルをダウンロード
2. foobar2000を起動
3. **ファイル → 設定 → Components → インストール...** を選択
4. ダウンロードした`.fb2k-component`ファイルを選択
5. foobar2000を再起動

### 方法2: ソースからビルド

詳細なビルド手順は[BUILD.md](BUILD.md)を参照してください。

必要な環境:

- Visual Studio 2022以降
- Windows 10/11
- foobar2000 SDK（リポジトリに含まれています）

## 使い方

### ダッシュボードを開く

1. foobar2000のメニューから **表示 → Monthly Stats Dashboard** を選択
2. ダッシュボードウィンドウに現在の月の統計が表示されます

### 統計を操作する

- **◀ / ▶ ボタン**: 前月/次月、前年/次年、または前日/次日に移動（選択されたビューに応じて変動）
- **Day/Month/Yearボタン**: 日次表示 → 月次表示 → 年次表示を循環
- **日次ナビゲーション**: Day ビューモードに切り替え、前日/次日矢印で日付をナビゲート
- **Resetボタン**: データベースから統計を再読み込み
- **Exportボタン**: HTMLレポートを生成
- **Export形式**: "Export: format" ボタンで Desktop/Smartphone（モバイル最適化）形式を切り替え
- **選択項目を除去**: 削除したいトラックを選択して「選択項目を除去」をクリック（元の履歴はデータベースに保持）

### レポートを表示する

エクスポートされたHTMLレポートには以下が含まれます:

- 期間のタイトル（例: "Monthly Stats – 2026-02" または "Yearly Stats – 2026"）
- 総再生時間のサマリー
- トップアーティストランキング（アルバムアートワーク付き）
- 全トラックリスト（再生回数と前期比較付き）
- シェアに最適化された美しいグラデーションデザイン

## 動作原理

- **再生トラッキング**: foobar2000の`play_callback` APIを使用して再生イベントを監視
- **正確な時間記録**: 実際の再生時間を記録（スキップや一時停止時はカウント停止）
- **バックグラウンド処理**: データベース操作は別スレッドで実行しUIをブロックしない
- **アルバムアート収集**: アルバムアートを抽出してbase64 JPEGに変換し、HTMLレポートに埋め込み

## 技術仕様

- **言語**: C++17
- **UIフレームワーク**: WTL (Windows Template Library) + ATL
- **データベース**: SQLite 3（amalgamation版）
- **XML処理**: pugixml
- **SDK**: foobar2000 SDK 2023
- **ビルドシステム**: MSBuild（カスタムDirectory.Build.props/targets使用）

## データベーススキーマ

コンポーネントはfoobar2000プロファイルディレクトリ内の`monthly_stats.db`にデータを保存します。

**メインテーブル: `monthly_count`**

- `ym`: 年月（例: "2026-02"）
- `track_crc`: 正規化されたトラックパスのCRC32ハッシュ
- `path`: 元のファイルパス
- `title`, `artist`, `album`: メタデータフィールド
- `playcount`: 再生回数
- `length_seconds`: 総再生時間（秒）

## ライセンス

このプロジェクトには様々なライセンスのコードとライブラリが含まれています:

- **foobar2000 SDK**: [sdk-license.txt](../sdk-license.txt)を参照
- **SQLite**: パブリックドメイン
- **pugixml**: MITライセンス
- **WTL**: Microsoft Public License
- **Catch2**（テスト用）: Boost Software License 1.0

プラグインコード自体はMITライセンスでリリースされています。

## 貢献

コントリビューションを歓迎します！以下の点にご注意ください:

- 既存のコードスタイル規則に従ってください
- x64 Releaseビルドでテストを実施してください
- データベーススキーマ変更時はマイグレーションロジックを含めてください

## 謝辞

- Peter Pawlowski氏によるfoobar2000とそのSDK
- D. Richard Hipp氏によるSQLite
- Arseny Kapoulkine氏によるpugixml
- MicrosoftによるWTL
