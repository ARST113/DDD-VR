param([Parameter(Mandatory=$true)][string]$LogPath)
$log = Get-Content -Raw -Path $LogPath
$checks=[ordered]@{
"Activity resume"=($log -match "nativeResume called");
"Activity pause before ready"=($log -match "ACTIVITY_PAUSED_BEFORE_XR_READY");
"XR READY"=($log -match "XR_SESSION_STATE_READY");
"xrBeginSession"=($log -match "xrBeginSession result=0");
"Loader init"=($log -match "LOADER_OK");
"xrCreateInstance"=($log -match "INSTANCE_OK");
"xrGetSystem"=($log -match "SYSTEM_OK");
"GL requirements"=($log -match "GL_REQUIREMENTS_OK");
"xrCreateSession"=($log -match "SESSION_OK");
"Reference space"=($log -match "REFERENCE_SPACE_OK");
"Swapchain"=($log -match "SWAPCHAIN_OK");
"FBO"=($log -match "FBO_OK");
"Frame loop"=($log -match "FRAME_LOOP_OK")
}
$frameLoop = $checks["Frame loop"]
$missingEarly = -not ($checks["Loader init"] -and $checks["xrCreateInstance"] -and $checks["xrGetSystem"] -and $checks["GL requirements"] -and $checks["xrCreateSession"] -and $checks["Reference space"] -and $checks["Swapchain"])
foreach($k in $checks.Keys){
  if($k -eq "Activity pause before ready"){ Write-Host "$k`t$(if($checks[$k]){'YES'}else{'NO'})"; continue }
  if($frameLoop -and $missingEarly -and $k -notin @("Frame loop","Activity resume","Activity pause before ready","XR READY","xrBeginSession") -and -not $checks[$k]){ Write-Host "$k`tDiagnostics incomplete: frame loop is alive, but init markers are missing." }
  else { Write-Host "$k`t$(if($checks[$k]){'PASS'}else{'FAIL'})" }
}
$blocker='unknown'
if($log -match 'ACTIVITY_PAUSED_BEFORE_XR_READY'){ $blocker='ACTIVITY_PAUSED_BEFORE_XR_READY' }
elseif($log -match 'SESSION_STUCK_IDLE_AFTER_SWAPCHAIN'){ $blocker='SESSION_STUCK_IDLE_AFTER_SWAPCHAIN' }
elseif($frameLoop){ $blocker='none at debug frame-loop level' }
Write-Host "CURRENT BLOCKER: $blocker"
