# Copilot Instructions for foo_monthly_stats (Updated)

このプロジェクトは、月間/年間の音楽視聴統計を追跡・視覚化する **foobar2000 プラグイン（C++17）** です。
以下の構成・手順を厳守し、**コードの生成や修正を行った後は、必ず末尾の `build.ps1` を実行して成功を確認してください。**

## 1. 環境構成

| 項目                 | 値                                            |
| -------------------- | --------------------------------------------- |
| **プラットフォーム** | x64 Release 専用                              |
| **ツールセット**     | `v145` (VS 2026 Preview) / CIでは `v143`      |
| **主要ライブラリ**   | WTL 10.0, foobar2000 SDK, SQLite 3, pugixml   |
| **コンパイラフラグ** | `/utf-8` (Directory.Build.targets で指定済み) |

---

## 2. プロジェクト構造と規約

### 重要なファイル配置

- `Directory.Build.props/targets`: 共通のビルドプロパティとインクルードパス。
- `foo_monthly_stats.rc`: **重要**。必ず **BOM付きUTF-16LE** で保存すること（ビルドエラー防止）。
- `stdafx.h`: `foobar2000+atl.h` を SDK ヘッダーより先に記述すること。

### 実装ルール

- **文字コード**: 入出力は UTF-8。UI表示時のみ UTF-16 に変換。
- **スレッド処理**: DBへの書き込みは UI をブロックしないようワーカースレッドで実行。
- **UI更新**: 処理完了後は `PostMessage` 等でメインスレッドに通知。

---

## 3. 開発完了後の必須ビルド・テスト手順

実装や修正が完了したら、プロジェクトルートにある統合ビルドスクリプト `build.ps1` を実行してください。このスクリプトは **「ビルド → テスト実行 → パッケージング」** を一括で行います。

### 実行コマンド

```powershell
# プロジェクトルートで実行
.\build.ps1 -Config Release -Platform x64

```

### スクリプトの主な処理内容

1. **MSBuild の自動検出**: `vswhere.exe` を使用して最適な MSBuild パスを特定。
2. **Step 1 (Build)**: `foo_monthly_stats.vcxproj` のビルド。
3. **Step 2 (Test)**: `tests.vcxproj` のビルドおよび `foo_monthly_stats_tests.exe` によるテスト実行。
4. **Step 3 (Package)**: `pack_component.py` による `.fb2k-component` の作成。

---

## 4. 自動実行の指示

**「コードの提案が終わったら、上記の手順に従って `build.ps1` を実行するよう私（ユーザー）に促してください。ビルドエラーやテスト失敗が発生した場合は、そのログを解析し、具体的な修正案を提示してください。」**
