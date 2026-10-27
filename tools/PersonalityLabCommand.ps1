param(
 [Parameter(Mandatory)][string]$LabRoot,
 [ValidateSet('say','episode','snapshot','consider','rebuild_dialogue','stop')][string]$Action='snapshot',
 [string]$Text='',
 [string]$Episode='',
 [int]$TimeoutSeconds=135
)
$ErrorActionPreference='Stop';$LabRoot=[IO.Path]::GetFullPath($LabRoot)
if($TimeoutSeconds-lt1-or$TimeoutSeconds-gt900){throw 'Command timeout must be 1..900 seconds'}
$manifest=Get-Content -LiteralPath (Join-Path $LabRoot controller.json) -Raw|ConvertFrom-Json
# A file alone is not evidence of a live controller. Check its retained process
# identity, including creation time so an old PID cannot target another process.
$process=Get-Process -Id $manifest.pid -ErrorAction SilentlyContinue
$started=if($manifest.process_started_utc-is[DateTime]){$manifest.process_started_utc.ToUniversalTime()}else{[DateTimeOffset]::Parse($manifest.process_started_utc).UtcDateTime}
if(!$manifest.running-or!$process-or[Math]::Abs(($process.StartTime.ToUniversalTime()-$started).TotalSeconds)-gt1){throw 'Controller is not running'}
$command=@{controller_id=$manifest.controller_id;action=$Action}
if($Action-eq'say'){if([string]::IsNullOrWhiteSpace($Text)-or$Text.Length-gt3000){throw 'Dialogue text must be 1..3000 characters'};$command.text=$Text}
if($Action-eq'episode'){$scene=Get-Content -LiteralPath $Episode -Raw|ConvertFrom-Json -AsHashtable;$command.turns=@($scene.turns);if($command.turns.Count-lt1-or$command.turns.Count-gt12){throw 'Conversation block must contain 1..12 turns'}}
$id=[Guid]::NewGuid().ToString('N');$inbox=Join-Path $LabRoot inbox;$outbox=Join-Path $LabRoot outbox
$temporary=Join-Path $inbox ($id+'.partial');$destination=Join-Path $inbox ($id+'.json');$responsePath=Join-Path $outbox ($id+'.json')
[IO.File]::WriteAllText($temporary,($command|ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false));Move-Item -LiteralPath $temporary -Destination $destination
$end=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
while(!(Test-Path -LiteralPath $responsePath)){
 $process.Refresh();if($process.HasExited){throw 'Controller exited before acknowledging this command'}
 if([DateTime]::UtcNow-ge$end){throw ('Command observation timed out; do not replay it blindly. Existing response path: '+$responsePath)}
 Start-Sleep -Milliseconds 100
}
$response=Get-Content -LiteralPath $responsePath -Raw|ConvertFrom-Json -Depth 85
if(!$response.ok){throw ('Lab command was rejected or failed. Evidence: '+$responsePath)}
$response.result|ConvertTo-Json -Depth 85
