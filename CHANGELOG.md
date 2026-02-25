# Release Notes

## v1.2.2 - 2026-02-25

### 🐛 Bug Fixes

- **Fixed HTML export not reflecting actual playback time**: HTML reports now display accurate listening time using `total_time_seconds`

---

## v1.2.2 - 2026-02-25（日本語）

### 🐛 バグ修正

- **HTMLエクスポートで実際の再生時間が反映されない問題を修正**: HTMLレポートが`total_time_seconds`を使用して正確な再生時間を表示するようになりました

---

## v1.2.1 - 2026-02-25

### 🐛 Bug Fixes

- **Fixed duplicate play count recording**: Play count and listening time are now correctly separated
  - `on_item_played`: Records play count only (played_seconds = 0)
  - `on_playback_stop`: Records actual playback time (played_seconds > 0)
  - Added `total_time_seconds` column to `monthly_count` table
  - Fixed issue where each play was counted twice due to both callbacks recording

### ✨ Enhancements

- **Improved time tracking accuracy**: Now tracks actual played time instead of full track length
  - Partial plays (stopped before completion) are now recorded with accurate duration
  - Loop playback iterations correctly accumulate actual playback time
  - Example: 80 seconds played = 80 seconds recorded (not the full track length)

- **Enhanced display precision**: Total listening time now shows seconds
  - Previous format: `1h 13m`
  - New format: `1h 13m 25s`

### 📝 Technical Notes

- Database schema update: Added `total_time_seconds` column (automatically migrated)
- `on_item_played` records playback events for play count (60-second rule)
- `on_playback_stop` records actual playback duration for listening time
- Aggregation queries updated to use `total_time_seconds` for accurate time calculation

---

## v1.2.1 - 2026-02-25（日本語）

### 🐛 バグ修正

- **重複する再生回数記録の修正**: 再生回数と再生時間を正しく分離
  - `on_item_played`: 再生回数のみ記録（played_seconds = 0）
  - `on_playback_stop`: 実際の再生時間を記録（played_seconds > 0）
  - `monthly_count`テーブルに`total_time_seconds`カラムを追加
  - 両方のコールバックが記録していたため、再生回数が2倍になっていた問題を修正

### ✨ 機能強化

- **再生時間トラッキングの精度向上**: トラック全体の長さではなく、実際に再生した時間を記録
  - 途中で停止した再生も正確な時間で記録されます
  - ループ再生の各イテレーションで実際の再生時間が正しく累積されます
  - 例：80秒再生 = 80秒記録（トラック全体の長さではなく）

- **表示精度の向上**: 総再生時間に秒数を表示
  - 以前のフォーマット：`1h 13m`
  - 新しいフォーマット：`1h 13m 25s`

### 📝 技術的な注記

- データベーススキーマ更新：`total_time_seconds`カラムを追加（自動マイグレーション）
- `on_item_played`は再生イベントを記録（60秒ルール適用）
- `on_playback_stop`は実際の再生時間を記録
- 集計クエリを更新し、正確な時間計算のため`total_time_seconds`を使用

---

## v1.2.0 - 2026-02-25

### 🔄 Breaking Changes

- **Migrated to foobar2000's standard playback statistics system**: Now uses `playback_statistics_collector` API instead of custom `play_callback_static` implementation
  - Recording threshold changed: Now records after **60 seconds** OR when track ends (if at least **1/3** was played)
  - Previous versions recorded after just 1 second of playback
  - This aligns with foobar2000's built-in Playback Statistics component behavior

### 🐛 Bug Fixes

- **Fixed loop playback recording**: Repeat (track) mode now correctly records multiple plays automatically
  - Previous implementation failed to detect loop iterations, only recording the first play
  - foobar2000's built-in statistics system now handles loop detection seamlessly
  - Each loop iteration is properly recorded when it meets the 60-second or 1/3-completion threshold

### ⚙️ Changes

- **Simplified and more reliable playback recording**: Removed complex manual loop detection logic (time jump detection, seek monitoring)
- **Better compatibility**: Now follows the same recording rules as other playback statistics components

### 📝 Technical Notes

- Replaced `play_callback_static` with `playback_statistics_collector`
- Removed `on_playback_time`, `on_playback_stop`, `on_playback_seek` implementations
- Now implements only `on_item_played()` callback
- Codebase significantly simplified by leveraging foobar2000's core playback statistics system
- Database schema remains unchanged (backward compatible)

### 🔧 Migration Notes

- **Important**: The 60-second threshold means very short tracks (< 20 seconds) may not be recorded if you skip them quickly
- Existing database and statistics are fully compatible
- For users relying on 1-second threshold: Consider this a more accurate representation of "listened" vs "skipped" tracks

---

## v1.2.0 - 2026-02-25（日本語）

### 🔄 破壊的変更

- **foobar2000標準の再生統計システムに移行**: カスタム実装の`play_callback_static`から`playback_statistics_collector` APIに変更
  - 記録の閾値が変更: **60秒再生**、または**トラックの1/3以上再生して終了**した場合に記録
  - 以前のバージョンでは1秒以上の再生で記録していました
  - foobar2000の組み込みPlayback Statisticsコンポーネントと同じ動作になります

### 🐛 バグ修正

- **ループ再生の記録を修正**: Repeat (track) モードで複数回の再生が正しく自動記録されるようになりました
  - 以前の実装ではループの繰り返しを検出できず、最初の1回しか記録されませんでした
  - foobar2000の組み込み統計システムがループ検出をシームレスに処理します
  - 各ループは60秒閾値または1/3完了閾値を満たすと正しく記録されます

### ⚙️ 変更点

- **シンプルで信頼性の高い再生記録**: 複雑な手動ループ検出ロジック（時間ジャンプ検出、シーク監視）を削除
- **互換性の向上**: 他の再生統計コンポーネントと同じ記録ルールに従います

### 📝 技術的な注意事項

- `play_callback_static`から`playback_statistics_collector`に置き換え
- `on_playback_time`、`on_playback_stop`、`on_playback_seek`の実装を削除
- `on_item_played()`コールバックのみを実装
- foobar2000のコア再生統計システムを活用することでコードベースを大幅に簡略化
- データベーススキーマは変更なし（後方互換性あり）

### 🔧 移行に関する注意事項

- **重要**: 60秒閾値により、非常に短いトラック（< 20秒）を素早くスキップした場合、記録されない可能性があります
- 既存のデータベースと統計は完全に互換性があります
- 1秒閾値に依存していたユーザーへ: これは「聴いた」vs「スキップした」トラックのより正確な表現と考えてください

---

## v1.1.1 - 2026-02-25

### 🐛 Bug Fixes

- **Fixed dashboard not updating in real-time**: The dashboard now automatically refreshes when playback stops, eliminating the need to restart foobar2000 to see new play statistics
- **Fixed missing UI synchronization**: Dashboard window now subscribes to playback events via `play_callback_manager`, ensuring the view stays up-to-date with recorded plays

### ⚙️ Changes

- **Renamed "Refresh" button to "Reset"**: Better reflects the button's actual function (deletes and recalculates monthly_count from play_log for the selected period)
- **Improved user experience**: No manual intervention needed to see newly recorded tracks in the dashboard

### 📝 Technical Notes

- Implemented `PlaybackCallbackImpl` class inheriting from `play_callback_impl_base`
- Dashboard registers `flag_on_playback_stop` callback on initialization
- Callback automatically invokes `Populate()` when playback stops, refreshing the list view
- Database schema remains unchanged (backward compatible with v1.0.0 and v1.1.0)
- `OnReset()` method renamed from `OnRefresh()` for clarity

### 🔧 Migration Notes

- No database migration required
- Existing installations will automatically benefit from real-time updates
- "Reset" button performs the same function as the previous "Refresh" button

---

## v1.1.1 - 2026-02-25（日本語）

### 🐛 バグ修正

- **ダッシュボードがリアルタイム更新されない問題を修正**: 再生停止時にダッシュボードが自動的に更新されるようになり、新しい再生統計を表示するためにfoobar2000を再起動する必要がなくなりました
- **UI同期の欠落を修正**: ダッシュボードウィンドウが`play_callback_manager`経由で再生イベントを購読し、記録された再生データとビューが常に同期されるようになりました

### ⚙️ 変更点

- **「Refresh」ボタンを「Reset」に改名**: ボタンの実際の機能（選択期間のmonthly_countをplay_logから削除→再計算）をより正確に反映
- **ユーザー体験の向上**: ダッシュボードに新しく記録されたトラックを表示するために手動操作が不要になりました

### 📝 技術的な注意事項

- `play_callback_impl_base`を継承した`PlaybackCallbackImpl`クラスを実装
- ダッシュボードは初期化時に`flag_on_playback_stop`コールバックを登録
- 再生停止時にコールバックが自動的に`Populate()`を呼び出し、リストビューを更新
- データベーススキーマは変更なし（v1.0.0およびv1.1.0と後方互換性あり）
- `OnRefresh()`メソッドを`OnReset()`に改名（明確化のため）

### 🔧 移行に関する注意事項

- データベース移行は不要
- 既存のインストールは自動的にリアルタイム更新の恩恵を受けます
- 「Reset」ボタンは以前の「Refresh」ボタンと同じ機能を実行します

---

## v1.1.0 - Initial tracked release

### Features

- Monthly statistics dashboard showing plays, playtime, and unique tracks
- Top 10 tracks and artists for each month
- Export functionality to CSV format
- SQLite database for persistent storage
- Real-time playback recording
