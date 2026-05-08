param(
    [Parameter(Mandatory = $true)]
    [string]$AdbPath,

    [Parameter(Mandatory = $true)]
    [string]$ApkPath,

    [string]$VideoUrl = "https://example.com/video.mp4"
)

$ErrorActionPreference = "Stop"

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-Adb {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Args,
        [switch]$AllowFailure
    )

    $output = & $AdbPath @Args 2>&1
    $exitCode = $LASTEXITCODE
    if (-not $AllowFailure -and $exitCode -ne 0) {
        throw "adb command failed ($exitCode): adb $($Args -join ' ')`n$output"
    }
    return [string]::Join("`n", $output)
}

$artifactsDir = Join-Path $PSScriptRoot "..\artifacts"
$artifactsDir = [System.IO.Path]::GetFullPath($artifactsDir)
New-Item -Path $artifactsDir -ItemType Directory -Force | Out-Null

$logPath = Join-Path $artifactsDir "pico-openxr-log.txt"
$filteredPath = Join-Path $artifactsDir "pico-openxr-filtered.txt"
$screenshotLocal = Join-Path $artifactsDir "pico-openxr.png"

Write-Host "[1/13] Проверка adb.exe..."
Assert-True -Condition (Test-Path -LiteralPath $AdbPath) -Message "adb.exe не найден: $AdbPath"

Write-Host "[2/13] adb devices..."
$devicesOutput = Invoke-Adb -Args @("devices")
Write-Host $devicesOutput

Write-Host "[3/13] Поиск подключенного устройства со статусом 'device'..."
$deviceLines = $devicesOutput -split "`r?`n" | Where-Object {
    $_ -match "\tdevice$"
}
Assert-True -Condition ($deviceLines.Count -gt 0) -Message "Не найдено устройство со статусом 'device'."

Write-Host "[4/13] Установка APK..."
Assert-True -Condition (Test-Path -LiteralPath $ApkPath) -Message "APK не найден: $ApkPath"
$installOutput = Invoke-Adb -Args @("install", "-r", $ApkPath)
Write-Host $installOutput

Write-Host "[5/13] Очистка logcat..."
Invoke-Adb -Args @("logcat", "-c") | Out-Null

Write-Host "[6/13] Force-stop приложения..."
Invoke-Adb -Args @("shell", "am", "force-stop", "top.rootu.dddvr") | Out-Null

Write-Host "[7/13] Запуск OpenXrPlayerActivity explicit intent..."
$startOutput = Invoke-Adb -Args @(
    "shell", "am", "start", "-W",
    "-a", "android.intent.action.VIEW",
    "-d", $VideoUrl,
    "-n", "top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity"
)
Write-Host $startOutput

Write-Host "[8/13] Проверка active activity через dumpsys..."
$activityDump = Invoke-Adb -Args @("shell", "dumpsys", "activity", "activities")
$activityActive = ($activityDump -match "top\.rootu\.dddvr/.xr\.activity\.OpenXrPlayerActivity")

Write-Host "[9/13] Ожидание 12 секунд для инициализации OpenXR..."
Start-Sleep -Seconds 12

Write-Host "[10/13] Снятие полного logcat..."
$fullLog = Invoke-Adb -Args @("logcat", "-d")
Set-Content -Path $logPath -Value $fullLog -Encoding UTF8

Write-Host "[11/13] Сохранение filtered log..."
$patterns = @(
    "DDDVR/OpenXR",
    "DDDVR/OpenXRLoader",
    "DDDVR/OpenXRSession",
    "DDDVR/OpenXRRenderer",
    "AndroidRuntime",
    "FATAL",
    "Exception",
    "UnsatisfiedLinkError"
)
$regex = ($patterns | ForEach-Object { [Regex]::Escape($_) }) -join "|"
$filtered = $fullLog -split "`r?`n" | Where-Object { $_ -match $regex }
Set-Content -Path $filteredPath -Value ($filtered -join "`n") -Encoding UTF8

Write-Host "[12/13] Screencap + pull (best-effort)..."
$screencapOk = $true
try {
    Invoke-Adb -Args @("shell", "screencap", "-p", "/sdcard/pico-openxr.png") | Out-Null
    Invoke-Adb -Args @("pull", "/sdcard/pico-openxr.png", $screenshotLocal) | Out-Null
} catch {
    $screencapOk = $false
    Write-Warning "Не удалось получить screenshot: $($_.Exception.Message)"
}

Write-Host "[13/13] PASS/FAIL проверка ключевых условий..."
$hasOpenXrUnavailable = $fullLog -match "OpenXR unavailable"
$hasOpenXrLoader = $fullLog -match "DDDVR/OpenXRLoader"
$hasXrInitializeLoader = $fullLog -match "xrInitializeLoaderKHR"
$hasXrCreateInstance = $fullLog -match "xrCreateInstance"
$hasRealXrError = $fullLog -match "\bxr[A-Za-z0-9_]+\b.*(error|failed|XR_ERROR)"
$hasFboGoodSign = $fullLog -match "fbo status=0x8cd5"

$conditions = [ordered]@{
    "OpenXrPlayerActivity активна" = $activityActive
    "Нет 'OpenXR unavailable'" = (-not $hasOpenXrUnavailable)
    "Есть OpenXRLoader" = $hasOpenXrLoader
    "Есть xrInitializeLoaderKHR" = $hasXrInitializeLoader
    "Есть xrCreateInstance ИЛИ реальная xr* ошибка" = ($hasXrCreateInstance -or $hasRealXrError)
}

$allPass = $true
foreach ($entry in $conditions.GetEnumerator()) {
    $status = if ($entry.Value) { "PASS" } else { "FAIL" }
    if (-not $entry.Value) { $allPass = $false }
    Write-Host ("{0}: {1}" -f $entry.Key, $status)
}

if ($hasFboGoodSign) {
    Write-Host "Признак рендера: обнаружен 'fbo status=0x8cd5' (good sign)."
} else {
    Write-Host "Признак рендера: 'fbo status=0x8cd5' не найден."
}

Write-Host "Artifacts:"
Write-Host "- $logPath"
Write-Host "- $filteredPath"
if ($screencapOk -and (Test-Path -LiteralPath $screenshotLocal)) {
    Write-Host "- $screenshotLocal"
}

if (-not $allPass) {
    throw "Smoke-test завершился с FAIL. См. логи в artifacts/."
}

Write-Host "Smoke-test PASS"
