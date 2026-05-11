param(
    [Parameter(Mandatory = $true)] [string]$AdbPath,
    [string]$ApkPath,
    [string]$VideoUrl = "https://example.com/video.mp4",
    [ValidateSet("both","direct","vr_entry_then_component","native_smoke")][string]$LaunchMode = "both"
)
$ErrorActionPreference = "Stop"
function Invoke-Adb { param([string[]]$Args,[switch]$AllowFailure); $o=& $AdbPath @Args 2>&1; if(-not $AllowFailure -and $LASTEXITCODE -ne 0){ throw "adb failed: $($Args -join ' ')`n$o"}; if($null -eq $o){return ""}; return ($o -join "`n") }
if([string]::IsNullOrWhiteSpace($ApkPath)){ $d=Join-Path $HOME "Downloads"; $c=Get-ChildItem $d -File -Recurse -ErrorAction SilentlyContinue | ? { $_.Name -like "*dddvr-debug-apk*" -or $_.Name -like "*DDD VR*" -or $_.Name -like "*DDD-VR*" -or $_.Name -like "*app-debug*.apk" } | sort LastWriteTime -desc; if($c){$ApkPath=$c[0].FullName}}
if(-not (Test-Path $AdbPath)){ throw "adb.exe не найден: $AdbPath" }
if(-not (Test-Path $ApkPath)){ throw "APK не найден: $ApkPath" }
$art= [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\artifacts")); New-Item $art -ItemType Directory -Force|Out-Null
Invoke-Adb @("devices") | Write-Host
Invoke-Adb @("install","-r",$ApkPath)|Write-Host

function Start-SmokeLaunch {
    param([string]$Mode)
    Invoke-Adb @("shell","logcat","-c")|Out-Null
    Invoke-Adb @("shell","am","force-stop","top.rootu.dddvr")|Out-Null
    if($Mode -eq "direct"){
        Write-Host "LAUNCH_MODE direct_component"
        Invoke-Adb @("shell","am","start","-W","-a","android.intent.action.VIEW","-d",$VideoUrl,"-n","top.rootu.dddvr/.xr.activity.OpenXrPlayerActivity","--ez","openxr_smoke_only","true")|Write-Host
    } else {
        Write-Host "LAUNCH_MODE vr_entry_then_component"
        Invoke-Adb @("shell","am","start","-W","-a","android.intent.action.MAIN","-c","android.intent.category.LAUNCHER","-n","top.rootu.dddvr/.vr.activity.VrPlayerActivity")|Write-Host
        Start-Sleep -Seconds 2
        Invoke-Adb @("shell","am","start","-W","-a","android.intent.action.VIEW","-d",$VideoUrl,"-n","top.rootu.dddvr/.xr.activity.OpenXrSmokeActivity")|Write-Host
    }
    Start-Sleep -Seconds 20
    return Invoke-Adb @("logcat","-d")
}

$fullParts=@()
if($LaunchMode -in @("both","direct")){
    $fullParts += "LAUNCH_MODE direct_component"
    $fullParts += Start-SmokeLaunch -Mode "direct"
}
if($LaunchMode -in @("both","vr_entry_then_component")){
    $fullParts += "LAUNCH_MODE vr_entry_then_component"
    $fullParts += Start-SmokeLaunch -Mode "launcher"
}
if($LaunchMode -eq "native_smoke"){
    $fullParts += "LAUNCH_MODE native_smoke"
    Invoke-Adb @("shell","logcat","-c")|Out-Null
    Invoke-Adb @("shell","am","force-stop","top.rootu.dddvr")|Out-Null
    Invoke-Adb @("shell","am","start","-W","-n","top.rootu.dddvr/.xr.activity.OpenXrNativeSmokeActivity")|Write-Host
    Start-Sleep -Seconds 20
    $fullParts += Invoke-Adb @("logcat","-d")
}
$full=($fullParts -join "`n")
Set-Content (Join-Path $art "pico-openxr-log.txt") $full -Encoding UTF8
$patterns=@("LAUNCH_MODE","NATIVE_SMOKE","OpenXrNativeSmoke","NATIVE_SMOKE_ANDROID_MAIN_BEGIN","NATIVE_SMOKE_INITIALIZE_BEGIN","NATIVE_SMOKE_START_BEGIN","XR_EVENT","SESSION_STATE_CHANGED","XR_CALL_BEGIN","XR_CALL_END","DDDVR/OpenXR","DDDVR/OpenXRLoader","DDDVR/OpenXRSession","DDDVR/OpenXRRenderer","DDDVR/OpenXRCheck","OpenXR-Loader","APxrRuntime","BD_ForwardLoader","XR_KHR_android_create_instance","setApplicationActivity","xrCreateInstance","xrGetSystem","xrGetOpenGLESGraphicsRequirementsKHR","xrCreateSession","xrCreateSwapchain","xrEnumerateSwapchainFormats","xrAcquireSwapchainImage","xrWaitSwapchainImage","xrReleaseSwapchainImage","xrWaitFrame","xrBeginFrame","xrLocateViews","xrEndFrame","fbo status","GL_FRAMEBUFFER","glErr","CURRENT_BLOCKER","AndroidRuntime","FATAL","Exception","UnsatisfiedLinkError","nativeResume called","nativePause called","nativeDestroy called","OpenXrApp::pause requested","OpenXrApp::resume requested","OpenXrApp::destroy requested","stopAndJoinThread reason","ACTIVITY_ON_START","ACTIVITY_ON_RESTART","ACTIVITY_ON_STOP","ACTIVITY_ON_NEW_INTENT","ACTIVITY_ON_USER_LEAVE_HINT","TOP_RESUMED_CHANGED","WINDOW_FOCUS_CHANGED","topResumeLossTimeout","pauseTimeout","ActivityTaskManager","WindowManager","PVR","Pico","boundary","guardian","seethrough","XR_START_STATE","xrStartState","XR_START_CALL_BEGIN","XR_START_CALL_END","NATIVE_START_RETURNED")
$re=($patterns|%{[Regex]::Escape($_)})-join "|"; ($full -split "`r?`n"|?{$_ -match $re}) -join "`n" | Set-Content (Join-Path $art "pico-openxr-filtered.txt") -Encoding UTF8
Invoke-Adb @("shell","screencap","-p","/sdcard/pico-openxr.png") -AllowFailure|Out-Null; Invoke-Adb @("pull","/sdcard/pico-openxr.png",(Join-Path $art "pico-openxr.png")) -AllowFailure|Out-Null
$checks=[ordered]@{
"Loader init"=($full -match "LOADER_OK");"xrCreateInstance"=($full -match "INSTANCE_OK");"xrGetSystem"=($full -match "SYSTEM_OK");"GL requirements"=($full -match "GL_REQUIREMENTS_OK");"EGL context"=($full -match "EGL context current");"xrCreateSession"=($full -match "SESSION_OK");"Reference space"=($full -match "REFERENCE_SPACE_OK");"Swapchain"=($full -match "SWAPCHAIN_OK");"FBO"=($full -match "FBO_OK");"Frame loop"=($full -match "FRAME_LOOP_OK")}
$hardFails=@("nativeCreate returned null","OpenXR bridge start failed","xrInitializeLoaderKHR failed","xrCreateInstance failed","xrCreateSession failed","No active XrInstance","UnsatisfiedLinkError","AndroidRuntime","FATAL EXCEPTION")
$all=$true; foreach($k in $checks.Keys){$v=$checks[$k]; if(-not $v){$all=$false}; Write-Host "$k: $(if($v){'PASS'}else{'FAIL'})"}
foreach($hf in $hardFails){ if($full -match [Regex]::Escape($hf)){ $all=$false; Write-Host "HardFail: $hf" } }
$blocker=($full -split "`r?`n"|?{$_ -match "CURRENT_BLOCKER"}|select -Last 1); Write-Host "CURRENT BLOCKER: $blocker"
Invoke-Adb @("shell","am","force-stop","top.rootu.dddvr") -AllowFailure|Out-Null
if(-not $all){ throw "Smoke-test FAIL" }
Write-Host "Smoke-test PASS"
