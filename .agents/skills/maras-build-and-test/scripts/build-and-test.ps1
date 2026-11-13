param(
    [ValidateSet("all", "tests", "dll")]
    [string]$Target = "all",

    [ValidateSet("release", "debug")]
    [string]$Preset = "release",

    [string]$Filter = "",

    [switch]$Reconfigure,

    [switch]$SkipHeaderCheck
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
$SkseDir  = Join-Path $RepoRoot "SKSE_Source"
$BuildDir = Join-Path $SkseDir "build\$Preset"
$Config   = if ($Preset -eq "debug") { "Debug" } else { "Release" }

$CMakeExe = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $CMakeExe)) {
    $fallback = Get-Command cmake -ErrorAction SilentlyContinue
    if ($fallback) {
        $CMakeExe = $fallback.Source
    } else {
        Write-Error "CMake executable not found at '$CMakeExe' or on PATH."
        exit 1
    }
}

if (-not $env:COMMONLIB_SSE_FOLDER) {
    $env:COMMONLIB_SSE_FOLDER = "E:/Skyrim/development/mods/_Sources/CommonLibSSE-NG"
}
if (-not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg"
}

# 1. Pure-module header hygiene check (zero engine / JSON / YAML / spdlog leaks in modules/ & tests/)
if (-not $SkipHeaderCheck) {
    $PurePaths = @(
        (Join-Path $SkseDir "modules"),
        (Join-Path $SkseDir "tests")
    ) | Where-Object { Test-Path $_ }

    if ($PurePaths.Count -gt 0) {
        $PureFiles = Get-ChildItem -Path $PurePaths -Recurse -Include "*.h","*.hpp","*.cpp"
        $Forbidden = $PureFiles | Select-String -Pattern '^\s*#\s*include\s*[<"](RE/|SKSE/|PCH\.h|nlohmann/|yaml-cpp/|spdlog/)'
        if ($Forbidden) {
            Write-Host "[FAIL] Forbidden engine/third-party #include detected in pure modules/tests:" -ForegroundColor Red
            $Forbidden | ForEach-Object { Write-Host ("  {0}:{1}: {2}" -f $_.Path, $_.LineNumber, $_.Line.Trim()) -ForegroundColor Red }
            exit 1
        }
        $PureHeaders = Get-ChildItem -Path $PurePaths -Recurse -Include "*.h","*.hpp"
        $UsingNs = $PureHeaders | Select-String -Pattern '^\s*using\s+namespace\s+'
        if ($UsingNs) {
            Write-Host "[FAIL] 'using namespace' detected in pure header:" -ForegroundColor Red
            $UsingNs | ForEach-Object { Write-Host ("  {0}:{1}: {2}" -f $_.Path, $_.LineNumber, $_.Line.Trim()) -ForegroundColor Red }
            exit 1
        }
    }
}

Push-Location $SkseDir
try {
    # 2. Configure only if build cache does not exist or -Reconfigure was requested
    $CacheFile = Join-Path $BuildDir "CMakeCache.txt"
    if ($Reconfigure -or -not (Test-Path $CacheFile)) {
        Write-Host "==> Configuring CMake preset '$Preset'..." -ForegroundColor Cyan
        & $CMakeExe --preset $Preset
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }

    # 3. Build requested targets
    if ($Target -eq "tests" -or $Target -eq "all") {
        Write-Host "==> Building maras_tests ($Config)..." -ForegroundColor Cyan
        & $CMakeExe --build $BuildDir --config $Config --target maras_tests
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }

    if ($Target -eq "dll" -or $Target -eq "all") {
        Write-Host "==> Building MARAS.dll ($Config)..." -ForegroundColor Cyan
        $DllBuiltPath = Join-Path $BuildDir "$Config\MARAS.dll"
        $PreBuildTime = if (Test-Path $DllBuiltPath) { (Get-Item $DllBuiltPath).LastWriteTimeUtc } else { [datetime]::MinValue }

        $BuildOutput = & $CMakeExe --build $BuildDir --config $Config --target MARAS 2>&1
        $DllExitCode = $LASTEXITCODE
        $BuildOutput | ForEach-Object { Write-Host $_ }

        if ($DllExitCode -ne 0) {
            $OutputText = ($BuildOutput | Out-String)
            $CompiledLinkLine = $OutputText -match "MARAS\.vcxproj\s*->\s*.*MARAS\.dll"
            $CopyFailedLine   = ($OutputText -match "Error copying file") -or ($OutputText -match "MSB3073") -or ($OutputText -match "cannot access the file")
            $DllExistsNow     = Test-Path $DllBuiltPath

            if ($DllExistsNow -and $CopyFailedLine -and ($CompiledLinkLine -or ((Get-Item $DllBuiltPath).LastWriteTimeUtc -ge $PreBuildTime))) {
                Write-Host "[WARN] MARAS.dll compiled & linked cleanly at '$DllBuiltPath', but POST_BUILD copy to 'SKSE/Plugins/MARAS.dll' failed (target DLL is likely locked by a running SkyrimSE.exe instance). Treating compilation as SUCCEEDED." -ForegroundColor Yellow
            } else {
                exit $DllExitCode
            }
        }
    }

    # 4. Run maras_tests.exe when tests were built
    if ($Target -eq "tests" -or $Target -eq "all") {
        $TestExe = Join-Path $BuildDir "$Config\maras_tests.exe"
        if (-not (Test-Path $TestExe)) {
            Write-Error "Expected test binary not found at '$TestExe'."
            exit 1
        }
        Write-Host "==> Running maras_tests.exe..." -ForegroundColor Cyan
        if ($Filter) {
            & $TestExe $Filter
        } else {
            & $TestExe
        }
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
} finally {
    Pop-Location
}
