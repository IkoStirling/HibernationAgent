param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot)
$ErrorActionPreference='Stop';$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot);$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
$root=Join-Path $ProjectRoot ('artifacts/personality-controller-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$node=if($env:HA_NODE_RUNTIME){$env:HA_NODE_RUNTIME}else{'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'}
$episode=Join-Path $root episode.json;@{version=1;title='controller lifecycle fixture';turns=@(@{text='合成记录：桌上的小故事先留着。'})}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $episode -Encoding utf8
$controller=$null;$lab=Join-Path $root continuing;$checks=0
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Start-Controller([int]$Seconds){
 $until=[DateTime]::UtcNow.AddSeconds($Seconds).ToString('O');$ceiling=[DateTime]::UtcNow.AddMinutes(3).ToString('O')
 $argsList=@('-NoProfile','-File',('"'+(Join-Path $ProjectRoot tools/PersonalityLab.ps1)+'"'),'-ProjectRoot',('"'+$ProjectRoot+'"'),'-BuildRoot',('"'+$BuildRoot+'"'),'-LabRoot',('"'+$lab+'"'),'-Episode',('"'+$episode+'"'),'-NodeExecutable',('"'+$node+'"'),'-Serve','-ServeUntilUtc',$until,'-DeadlineUtc',$ceiling)
 $script:controller=Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList $argsList -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root ('controller-'+$Seconds+'.log')) -RedirectStandardError (Join-Path $root ('controller-'+$Seconds+'-error.log'))
}
function Command($Action,$Text=''){
 $result=& (Join-Path $ProjectRoot tools/PersonalityLabCommand.ps1) -LabRoot $lab -Action $Action -Text $Text -TimeoutSeconds 25
 $result|ConvertFrom-Json -Depth 85
}
try{
 Start-Controller 90
 Await {if($controller.HasExited){throw 'Controller startup failed'};if(Test-Path -LiteralPath (Join-Path $lab controller.json)){$x=Get-Content -LiteralPath (Join-Path $lab controller.json) -Raw|ConvertFrom-Json;return $x.pid-eq$controller.Id-and$x.running};$false} 'live controller'
 $before=Command snapshot;Check ($before.activities.enabled-and$before.backend_pid-gt0) 'continuous private desktop and autonomous plugin context are live'
 $said=Command say '合成记录：那只纸船现在先别动。';Check (!$said.failed-and$said.reply) 'message command reaches the same continuing role'
 $after=Command snapshot;Check ($after.backend_pid-eq$before.backend_pid-and$after.turn_count-eq$before.turn_count+1) 'conversation continues without restarting the backend'
 $rebuilt=Command rebuild_dialogue;$preserved=Command snapshot
 Check ($rebuilt.rebuilt-and$rebuilt.history_retained-and$rebuilt.state_retained-and$preserved.dialogue_message_count-eq$after.dialogue_message_count-and$preserved.persona_version-eq$after.persona_version-and$preserved.stage_id-eq$after.stage_id) 'an explicit lab intervention rebuilds working dialogue while retaining actual history persona and stage'
 $manifest=Get-Content -LiteralPath (Join-Path $lab controller.json) -Raw|ConvertFrom-Json
 $id=[Guid]::NewGuid().ToString('N');$marker=Join-Path $root must-not-execute.txt
 $partial=Join-Path $lab ('inbox/'+$id+'.partial');@{controller_id=$manifest.controller_id;action='execute';text=('Set-Content '+$marker+' forbidden')}|ConvertTo-Json|Set-Content -LiteralPath $partial -Encoding utf8
 Move-Item -LiteralPath $partial -Destination (Join-Path $lab ('inbox/'+$id+'.json'))
 Await {Test-Path -LiteralPath (Join-Path $lab ('outbox/'+$id+'.json'))} 'rejected command response'
 $rejected=Get-Content -LiteralPath (Join-Path $lab ('outbox/'+$id+'.json')) -Raw|ConvertFrom-Json
 Check (!$rejected.ok-and!(Test-Path -LiteralPath $marker)) 'mailbox never executes arbitrary scripts or unknown actions'
 $null=Command stop;Check ($controller.WaitForExit(12000)-and$controller.ExitCode-eq0) 'stop command completes clean controller shutdown'
 $saved=Get-Content -LiteralPath (Join-Path $lab controller.json) -Raw|ConvertFrom-Json
 Check (!$saved.running-and!(Get-Process -Id $saved.backend_pid -ErrorAction SilentlyContinue)-and!(Test-Path -LiteralPath (Join-Path $lab runner.lock))) 'child desktop and archive ownership are released'
 Check (!(Get-Content -LiteralPath (Join-Path $root controller-90.log) -Raw).Contains('personality-lab-')) 'local session token is absent from controller logs'
 Check (!((Get-Content -LiteralPath (Join-Path $lab controller.json) -Raw)-match'(?i)authorization|credential|session_key')) 'controller manifest contains no session or provider credential'
 @{version=1;title='maintenance resume without a new user event';turns=@()}|ConvertTo-Json|Set-Content -LiteralPath $episode -Encoding utf8
 Start-Controller 12
 Await {if($controller.HasExited){throw 'Controller resume failed'};$x=Get-Content -LiteralPath (Join-Path $lab controller.json) -Raw|ConvertFrom-Json;$x.pid-eq$controller.Id-and$x.running} 'resumed controller'
 $resumed=Command snapshot;Check ($resumed.turn_count-eq0-and$resumed.dialogue_message_count-eq$preserved.dialogue_message_count) 'maintenance resume can preserve the same life without inventing another user conversation'
 Check ($controller.WaitForExit(22000)-and$controller.ExitCode-eq0) 'real deadline ends a continuing controller without another command'
 $saved=Get-Content -LiteralPath (Join-Path $lab controller.json) -Raw|ConvertFrom-Json;Check (!$saved.running-and!(Get-Process -Id $saved.backend_pid -ErrorAction SilentlyContinue)) 'deadline also stops its child backend'
 Write-Output "Personality controller passed: $checks checks. Evidence: $root"
}finally{
 if($controller){$controller.Refresh();if(!$controller.HasExited){try{$null=Command stop;$null=$controller.WaitForExit(5000)}catch{};if(!$controller.HasExited){Stop-Process -Id $controller.Id}}}
}
