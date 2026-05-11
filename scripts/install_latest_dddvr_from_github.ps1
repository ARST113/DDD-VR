param([Parameter(Mandatory=$true)][string]$LogPath)
$log = Get-Content -Raw -Path $LogPath
$checks=[ordered]@{
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
  if($frameLoop -and $missingEarly -and $k -ne "Frame loop" -and -not $checks[$k]){ Write-Host "$k`tDiagnostics incomplete: frame loop is alive, but init markers are missing." }
  else { Write-Host "$k`t$(if($checks[$k]){'PASS'}else{'FAIL'})" }
}
Write-Host "CURRENT BLOCKER: $(if($frameLoop){'none at debug frame-loop level'}else{'unknown'})"
