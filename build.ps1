param(
    [string]$VSPath,
    [string]$Config = "Release",
    [string]$Platform = "x64"
)

# VS のインストール先に VsDevCmd.bat がない場合、MSBuild を直接探す
if (-not $VSPath) {
    $VSPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
    Write-Host "Found VS at: $VSPath"
}

# MSBuild のパスを構築
$MSBuildPath = Join-Path $VSPath "MSBuild\Current\Bin\MSBuild.exe"

if (-not (Test-Path $MSBuildPath)) {
    Write-Error "MSBuild not found at $MSBuildPath"
    exit 1
}

Write-Host "MSBuild found at: $MSBuildPath"

# Step 1: ビルド
Write-Host "`n=== Step 1: Building foo_monthly_stats.dll ===" -ForegroundColor Green
& $MSBuildPath "foo_monthly_stats\foo_monthly_stats.vcxproj" "/p:Configuration=$Config" "/p:Platform=$Platform" "/nologo"

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed"
    exit 1
}

# Step 2: テストのビルドと実行
Write-Host "`n=== Step 2: Building and running tests ===" -ForegroundColor Green
& $MSBuildPath "foo_monthly_stats\tests\tests.vcxproj" "/p:Configuration=$Config" "/p:Platform=$Platform" "/nologo"

if ($LASTEXITCODE -ne 0) {
    Write-Error "Test build failed"
    exit 1
}

$TestExe = "foo_monthly_stats\tests\_result\x64_Release\tests\foo_monthly_stats_tests.exe"
if (Test-Path $TestExe) {
    & $TestExe "--reporter" "compact"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Tests failed"
        exit 1
    }
} else {
    Write-Warning "Test executable not found at $TestExe"
}

# Step 3: コンポーネント パッケージング
Write-Host "`n=== Step 3: Packaging component ===" -ForegroundColor Green
if (Test-Path "foo_monthly_stats\scripts\pack_component.py") {
    python "foo_monthly_stats\scripts\pack_component.py" "--platform" "x64" "--configuration" "Release"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Packaging failed"
        exit 1
    }
} else {
    Write-Warning "pack_component.py not found"
}

Write-Host "`n=== Build Complete ===" -ForegroundColor Green
Write-Host "DLL: foo_monthly_stats\_result\x64_Release\bin\foo_monthly_stats.dll"
Write-Host "Component: foo_monthly_stats\_result\foo_monthly_stats.fb2k-component"
