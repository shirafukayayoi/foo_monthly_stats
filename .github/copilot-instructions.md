# Copilot Instructions for foo_monthly_stats

このプロジェクトは、月間/年間の音楽視聴統計を追跡・視覚化する **foobar2000 プラグイン（C++17）** です。
以下の構成・手順を厳守し、**コードの生成や修正を行った後は、必ず末尾のビルド手順を実行して成功を確認してください。**

## 1. 環境構成

| 項目                 | 値                                            |
| :------------------- | :-------------------------------------------- |
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

実装や修正が完了したら、以下の PowerShell コマンドを順番に実行し、すべての工程が成功することを確認してください。

### ① クリーン & DLL ビルド

```powershell
# 古い中間ファイルの削除
cd "C:\Users\ron06\OneDrive\Documents\Program\GitHub\fb2k\foo_monthly_stats"
Remove-Item -Recurse -Force x64_Release -ErrorAction SilentlyContinue

# ビルド実行
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
    foo_monthly_stats.vcxproj /p:Configuration=Release /p:Platform=x64 /nologo

```

### ② テスト実行

```powershell
# テストのビルド
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
    tests\tests.vcxproj /p:Configuration=Release /p:Platform=x64 /nologo

# テストランナーの起動
.\tests\_result\x64_Release\tests\foo_monthly_stats_tests.exe --reporter compact

```

### ③ パッケージング

```powershell
python scripts\pack_component.py --platform x64 --configuration Release

```

---

## 4. 自動実行の指示

**「コードの提案が終わったら、上記の手順に従ってビルドとテストを代行、または実行するよう私（ユーザー）に促してください。ビルドエラーが発生した場合は、そのエラー内容を解析し、修正案を提示してください。」**

```

```
