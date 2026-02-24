# foo_monthly_stats ビルドガイド

## 環境

| 項目                         | 値                                   |
| ---------------------------- | ------------------------------------ |
| IDE                          | Visual Studio 2022 / VS 2026 Preview |
| プラットフォームツールセット | `v145` (MSVC 14.50.35717)            |
| 対象アーキテクチャ           | x64                                  |
| C++ 標準                     | C++17                                |
| OS                           | Windows 10/11                        |

## 前提条件

- **Visual Studio 2022+** (ワークロード: C++ デスクトップ開発、ATL 含む)
- **WTL 10.0** — `fb2k/third_party/wtl/Include/` に配置済み
- **foobar2000 SDK** — リポジトリ内の `foobar2000/` ディレクトリ
- **Python 3** — コンポーネントパッケージング用

## プロジェクト構成

```
fb2k/
├── Directory.Build.props        # 共通ビルドプロパティ
├── Directory.Build.targets      # 共通インクルードパス・コンパイルオプション
├── foobar2000/
│   ├── shared/shared-x64.lib   # foobar2000 リンクライブラリ
│   └── ...
└── foo_monthly_stats/
    ├── foo_monthly_stats.vcxproj
    ├── third_party/
    │   ├── sqlite/              # SQLite 3 amalgamation
    │   └── pugixml/             # pugixml XML ライブラリ
    ├── tests/
    │   ├── tests.vcxproj
    │   └── catch2/              # Catch2 amalgamation
    └── scripts/
        └── pack_component.py
```

## ビルド手順

### 1. プラグイン本体 (DLL)

```powershell
# foo_monthly_stats ディレクトリへ移動
cd C:\Users\ron06\OneDrive\Documents\Program\GitHub\fb2k\foo_monthly_stats

# クリーンビルド（推奨）
Remove-Item -Recurse -Force x64_Release -ErrorAction SilentlyContinue

# MSBuild でビルド
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
    foo_monthly_stats.vcxproj `
    /p:Configuration=Release /p:Platform=x64 `
    /nologo
```

成功すると以下に出力されます:

```
_result\x64_Release\bin\foo_monthly_stats.dll
_result\x64_Release\bin\foo_monthly_stats.pdb
```

### 2. テスト

```powershell
# テストプロジェクトをビルド
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
    tests\tests.vcxproj `
    /p:Configuration=Release /p:Platform=x64 `
    /nologo

# テスト実行
.\tests\_result\x64_Release\tests\foo_monthly_stats_tests.exe --reporter compact
```

期待される出力:

```
All tests passed (25 assertions in 7 test cases)
```

### 3. パッケージング

```powershell
python scripts\pack_component.py --platform x64 --configuration Release
```

成功すると以下が生成されます:

```
_result\foo_monthly_stats.fb2k-component   (~793 KB)
```

## インストール

生成された `.fb2k-component` ファイルを foobar2000 にインストールします:

1. foobar2000 を起動
2. メニュー → **ファイル → コンポーネントのインストール...** を選択
3. `_result\foo_monthly_stats.fb2k-component` を選択
4. foobar2000 を再起動

または、`foo_monthly_stats.dll` を foobar2000 の `components\` フォルダに直接コピーしても構いません。

---

## ビルド時に適用した修正メモ

### Directory.Build.targets への `/utf-8` フラグ追加

`.cpp` / `.h` ソースファイル内の非 ASCII 文字列リテラル（日本語・ギリシャ文字）を正しくコンパイルするため、
`Directory.Build.targets` の `ClCompile` に以下を追加:

```xml
<AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>
```

### `.rc` ファイルの文字コード

Windows Resource Compiler (`rc.exe`) は **UTF-16LE (BOM付き)** のみ受け付けます。
`foo_monthly_stats.rc` を保存する際は UTF-16LE で保存してください。

PowerShell での変換方法:

```powershell
$file = "foo_monthly_stats.rc"
$content = [System.IO.File]::ReadAllText($file, [System.Text.Encoding]::UTF8)
[System.IO.File]::WriteAllText($file, $content, [System.Text.Encoding]::Unicode)
```

### PCH (プリコンパイル済みヘッダー) がロックされた場合

ビルドが途中で中断された後に `error C3859` や `LNK1104` が出る場合:

```powershell
# 前のビルドの中間ファイルを削除してからリビルド
Remove-Item -Recurse -Force x64_Release -ErrorAction SilentlyContinue
```

---

## 依存ライブラリライセンス

| ライブラリ     | ライセンス                            |
| -------------- | ------------------------------------- |
| foobar2000 SDK | [sdk-license.txt](../sdk-license.txt) |
| SQLite         | パブリックドメイン                    |
| pugixml        | MIT                                   |
| WTL            | Microsoft Public License              |
| Catch2         | BSL-1.0                               |
