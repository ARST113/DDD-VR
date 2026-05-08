param(
    [Parameter(Mandatory = $true)] [string]$AdbPath,
    [string]$ApkPath,
    [string]$VideoUrl = "https://example.com/video.mp4"
)
$ErrorActionPreference = "Stop"
function Invoke-Adb { param([string[]]$Args,[switch]$AllowFailure); $o=& $AdbPath @Args 2>&1; if(-not $AllowFailure -and $LASTEXITCODE -ne 0){ throw "adb failed: $($Args -join ' ')`n$o"}; if($null -eq $o){return ""}; return ($o -join "`n") }
if([string]::IsNullOrWhiteSpace($ApkPath)){ $d=Join-Path $HOME "Downloads"; $c=Get-ChildItem $d -File -Recurse -ErrorAction SilentlyContinue | ? { $_.Name -like "*dddvr-debug-apk*" -or $_.Name -like "*DDD VR*" -or $_.Name -like "*DDD-VR*" -or $_.Name -like "*app-debug*.apk" } | sort LastWriteTime -desc; if($c){$ApkPath=$c[0].FullName}}
if(-not (Test-Path $AdbPath)){ throw "adb.exe не найден: $AdbPath" }
if(-not (Test-Path $ApkPath)){ throw "APK не найден: $ApkPath" }
$art= [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\artifacts")); New-Item $art -ItemType Directory -Force|Out-Null
Invoke-Adb @("devices") | Write-Host
Invoke-Adb @("install","-r",$ApkPath)|Write-Host
Invoke-Adb @("logcat","-c")|Out-Null
Invoke-Adb @("shell","am","force-stop","top.rootu.dddvr")|Out-Null
Invoke-Adb @("shell","am","start","-W","-a","android.intent.action.VIEW","-d",$VideoUrl,"-n","top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity")|Write-Host
Start-Sleep -Seconds 12
$full=Invoke-Adb @("logcat","-d"); Set-Content (Join-Path $art "pico-openxr-log.txt") $full -Encoding UTF8
$patterns=@("DDDVR/OpenXR","DDDVR/OpenXRLoader","DDDVR/OpenXRSession","DDDVR/OpenXRRenderer","OpenXR-Loader","APxrRuntime","BD_ForwardLoader","XR_KHR_android_create_instance","setApplicationActivity","xrCreateInstance","xrGetSystem","xrGetOpenGLESGraphicsRequirementsKHR","xrCreateSession","xrCreateSwapchain","xrEnumerateSwapchainFormats","xrAcquireSwapchainImage","xrWaitSwapchainImage","xrReleaseSwapchainImage","xrWaitFrame","xrBeginFrame","xrLocateViews","xrEndFrame","fbo status","GL_FRAMEBUFFER","glErr","CURRENT_BLOCKER","AndroidRuntime","FATAL","Exception","UnsatisfiedLinkError")
$re=($patterns|%{[Regex]::Escape($_)})-join "|"; ($full -split "`r?`n"|?{$_ -match $re}) -join "`n" | Set-Content (Join-Path $art "pico-openxr-filtered.txt") -Encoding UTF8
Invoke-Adb @("shell","screencap","-p","/sdcard/pico-openxr.png") -AllowFailure|Out-Null; Invoke-Adb @("pull","/sdcard/pico-openxr.png",(Join-Path $art "pico-openxr.png")) -AllowFailure|Out-Null
$checks=[ordered]@{
"Loader init"=($full -match "initializeLoader result=true");"xrCreateInstance"=($full -match "xrCreateInstance result=0");"xrGetSystem"=($full -match "xrGetSystem result=0");"GL requirements"=($full -match "xrGetOpenGLESGraphicsRequirementsKHR result=0");"EGL context"=($full -match "EGL context current");"xrCreateSession"=($full -match "xrCreateSession result=0");"Reference space"=($full -match "xrCreateReferenceSpace result=0");"Swapchain"=($full -match "xrCreateSwapchain result=0");"FBO"=($full -match "fbo status=0x8cd5");"Frame loop"=(($full -match "xrWaitFrame=0") -and ($full -match "xrBeginFrame=0") -and ($full -match "xrEndFrame=0"))}
$all=$true; foreach($k in $checks.Keys){$v=$checks[$k]; if(-not $v){$all=$false}; Write-Host "$k: $(if($v){'PASS'}else{'FAIL'})"}
$blocker=($full -split "`r?`n"|?{$_ -match "CURRENT_BLOCKER"}|select -Last 1); Write-Host "CURRENT BLOCKER: $blocker"
Invoke-Adb @("shell","am","force-stop","top.rootu.dddvr") -AllowFailure|Out-Null
if(-not $all){ throw "Smoke-test FAIL" }
Write-Host "Smoke-test PASS"
