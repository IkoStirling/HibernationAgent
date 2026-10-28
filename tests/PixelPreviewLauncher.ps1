param([Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$BuildRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$PSNativeCommandUseErrorActionPreference=$false;$checks=0;$origin=$null;$headers=$null;$ownedProcess=$null
Add-Type -AssemblyName System.Security
$root=Join-Path ([IO.Path]::GetFullPath($ScratchRoot)) ('pixel-preview-launcher-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root
$runtime=Join-Path $root runtime;New-Item -ItemType Directory -Force $runtime|Out-Null
Copy-Item -LiteralPath (Join-Path $BuildRoot hibernation-agent.exe) -Destination $runtime
Get-ChildItem -LiteralPath $BuildRoot -File -Filter '*.dll'|ForEach-Object {Copy-Item -LiteralPath $_.FullName -Destination $runtime}
New-Item -ItemType Directory -Force (Join-Path $runtime plugins)|Out-Null
Copy-Item -LiteralPath (Join-Path $BuildRoot plugins/pixel-canvas) -Destination (Join-Path $runtime plugins) -Recurse
$launcher=Join-Path $ProjectRoot scripts/StartPixelCanvasPreview.ps1;$pwsh=Join-Path $PSHOME pwsh.exe
if(!(Test-Path -LiteralPath $launcher)){throw 'preview launcher source missing'}
$workspace=Join-Path $root preview;$configPath=Join-Path $workspace config.json;$readyPath=Join-Path $workspace data/runtime/ready.json;$run=0
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 60};throw "TIMEOUT $Name"}
function Launch([string]$Target,[string[]]$Flags,[string]$UseBuildRoot=$runtime,[string]$UseAssetRoot=$ProjectRoot){
 $script:run++;$log=Join-Path $root ('launcher-'+$script:run+'.log')
 # A background service can inherit native pipe handles. Wait for the launcher
 # process itself, not stdout EOF or Start-Process -Wait's descendant tree.
 $arguments=@('-NoProfile','-File',('"'+$launcher+'"'),'-BuildRoot',('"'+$UseBuildRoot+'"'),'-AssetRoot',('"'+$UseAssetRoot+'"'),'-WorkspaceRoot',('"'+$Target+'"'))+$Flags
 $child=Start-Process -FilePath $pwsh -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError ($log+'.error')
 if(!$child.WaitForExit(30000)){$child.Kill();throw 'preview launcher process did not return'}
 return @{code=$child.ExitCode;log=$log}
}
function Api($Path){Invoke-RestMethod -Uri ($script:origin+$Path) -Headers $script:headers -TimeoutSec 8}
function ConfigurationBytes{[IO.File]::ReadAllBytes($configPath)}
function SameBytes([byte[]]$A,[byte[]]$B){[Convert]::ToBase64String($A)-ceq[Convert]::ToBase64String($B)}
try{
 $check=Launch $workspace @('-CheckConfig');Check ($check.code-eq0-and(Test-Path -LiteralPath $configPath)) 'check-config creates a fresh owned preview configuration'
 Check (([IO.File]::ReadAllText((Join-Path $workspace .pixel-canvas-preview))).Trim()-eq'hibernation.pixel-canvas.preview.v1') 'preview workspace has an explicit ownership marker'
 Check (!(Test-Path -LiteralPath (Join-Path $workspace data))-and!(Test-Path -LiteralPath (Join-Path $workspace drafts))-and!(Test-Path -LiteralPath (Join-Path $workspace materials))) 'check-config does not open archives create drafts or acquire model credentials'
 $config=Get-Content -LiteralPath $configPath -Raw|ConvertFrom-Json -AsHashtable
 Check ($config.profiles.Count-eq1-and$config.profiles[0].id-eq'mock'-and$config.profiles[0].protocol-eq'mock'-and$config.profiles[0].enabled) 'fresh preview uses only the offline mock connection'
 Check ($config.plugins.Count-eq1-and$config.plugins.ContainsKey('pixel-canvas')-and$config.plugins.'pixel-canvas'.enabled-and!$config.computer.enabled-and!$config.remoteStorage.enabled) 'fresh preview enables only the pixel plugin with computer and remote storage off'
 Check ([IO.Path]::GetFullPath($config.dataRoot)-eq[IO.Path]::GetFullPath((Join-Path $workspace data))-and[IO.Path]::GetFullPath($config.draftsRoot)-eq[IO.Path]::GetFullPath((Join-Path $workspace drafts))-and[IO.Path]::GetFullPath($config.materialsRoot)-eq[IO.Path]::GetFullPath((Join-Path $workspace materials))) 'all writable roots are isolated inside the preview workspace'
 Check ([IO.Path]::GetFullPath($config.characterRoot)-eq[IO.Path]::GetFullPath((Join-Path $ProjectRoot character))-and[IO.Path]::GetFullPath($config.uiRoot)-eq[IO.Path]::GetFullPath((Join-Path $ProjectRoot ui))) 'read-only assets use the explicitly frozen source root'
 $config.profiles[0].label='操作者保留的预览设置';$config.plugins.'pixel-canvas'.defaultScale=4
 [IO.File]::WriteAllText($configPath,($config|ConvertTo-Json -Depth 64),[Text.UTF8Encoding]::new($false));$preserved=ConfigurationBytes
 $again=Launch $workspace @('-CheckConfig');Check ($again.code-eq0-and(SameBytes $preserved (ConfigurationBytes))) 'a second check preserves existing preview settings byte for byte'
 $otherAssets=Join-Path $root other-assets
 foreach($relative in @('config/example.json','character/persona.json','ui/pixel-canvas.js','ui/pixel-canvas-examples.js','scripts/Start.ps1','scripts/Stop.ps1')){$target=Join-Path $otherAssets $relative;New-Item -ItemType Directory -Force (Split-Path $target)|Out-Null;Copy-Item -LiteralPath (Join-Path $ProjectRoot $relative) -Destination $target}
 $refusal=Launch $workspace @('-CheckConfig') $runtime $otherAssets;Check ($refusal.code-ne0-and(SameBytes $preserved (ConfigurationBytes))) 'existing preview refuses a different asset root without rebinding its configuration'
 $otherBuild=Join-Path $root other-build;New-Item -ItemType Directory -Path $otherBuild|Out-Null;Copy-Item -LiteralPath (Join-Path $runtime hibernation-agent.exe) -Destination $otherBuild
 $refusal=Launch $workspace @('-CheckConfig') $otherBuild $ProjectRoot;Check ($refusal.code-ne0-and(SameBytes $preserved (ConfigurationBytes))) 'existing preview refuses a different program root without rebinding its configuration'
 $unowned=Join-Path $root unowned;New-Item -ItemType Directory -Path $unowned|Out-Null;[IO.File]::WriteAllText((Join-Path $unowned sentinel.txt),'unowned original content')
 $refusal=Launch $unowned @('-CheckConfig');Check ($refusal.code-ne0-and[IO.File]::ReadAllText((Join-Path $unowned sentinel.txt))-eq'unowned original content'-and@(Get-ChildItem -LiteralPath $unowned -Force).Count-eq1) 'unowned existing directory is refused without writing configuration or a marker'
 $wrong=Join-Path $root wrong-marker;New-Item -ItemType Directory -Path $wrong|Out-Null;[IO.File]::WriteAllText((Join-Path $wrong .pixel-canvas-preview),'another application owns this directory')
 $refusal=Launch $wrong @('-CheckConfig');Check ($refusal.code-ne0-and[IO.File]::ReadAllText((Join-Path $wrong .pixel-canvas-preview))-eq'another application owns this directory'-and@(Get-ChildItem -LiteralPath $wrong -Force).Count-eq1) 'wrong ownership marker is refused without changing its contents'
 $foreign=Join-Path $root foreign-data;New-Item -ItemType Directory -Path $foreign|Out-Null;[IO.File]::WriteAllText((Join-Path $foreign sentinel.txt),'outside preview data')
 foreach($field in @('dataRoot','draftsRoot','materialsRoot')){
  $redirected=Get-Content -LiteralPath $configPath -Raw|ConvertFrom-Json -AsHashtable;$redirected[$field]=$foreign;[IO.File]::WriteAllText($configPath,($redirected|ConvertTo-Json -Depth 64),[Text.UTF8Encoding]::new($false));$changed=ConfigurationBytes
  $refusal=Launch $workspace @('-CheckConfig');Check ($refusal.code-ne0-and(SameBytes $changed (ConfigurationBytes))-and@(Get-ChildItem -LiteralPath $foreign -Force).Count-eq1) ("existing preview refuses external $field without resetting settings or touching the target")
  [IO.File]::WriteAllBytes($configPath,$preserved)
 }
 $link=Join-Path $workspace redirected-link;$junction=$false
 try{$null=New-Item -ItemType Junction -Path $link -Target $foreign;$junction=$true}catch{Write-Output 'Junction creation unavailable; no junction acceptance is claimed.'}
 if($junction){
  $redirected=Get-Content -LiteralPath $configPath -Raw|ConvertFrom-Json -AsHashtable;$redirected.draftsRoot=Join-Path $link nested-drafts;[IO.File]::WriteAllText($configPath,($redirected|ConvertTo-Json -Depth 64),[Text.UTF8Encoding]::new($false));$changed=ConfigurationBytes
  $refusal=Launch $workspace @('-CheckConfig');Check ($refusal.code-ne0-and(SameBytes $changed (ConfigurationBytes))-and@(Get-ChildItem -LiteralPath $foreign -Force).Count-eq1) 'an in-tree junction ancestor cannot redirect preview drafts outside its workspace'
  [IO.File]::WriteAllBytes($configPath,$preserved)
  $nested=Join-Path $root nested-archive-preview;$null=Launch $nested @('-CheckConfig');New-Item -ItemType Directory -Force (Join-Path $nested data)|Out-Null
  $null=New-Item -ItemType Junction -Path (Join-Path $nested data/archive) -Target $foreign
  $refusal=Launch $nested @('-CheckConfig');Check ($refusal.code-ne0-and@(Get-ChildItem -LiteralPath $foreign -Force).Count-eq1-and!(Test-Path -LiteralPath (Join-Path $foreign records.sqlite))) 'an otherwise valid data root cannot contain an archive junction to external data'
 }
 $never=Join-Path $root never-started;$stopped=Launch $never @('-Stop');Check ($stopped.code-eq0-and!(Test-Path -LiteralPath $never)) 'stopping an unstarted preview is friendly and does not create a workspace'
 $stopped=Launch $workspace @('-Stop');Check ($stopped.code-eq0-and!(Test-Path -LiteralPath (Join-Path $workspace data))) 'stopping a checked but unstarted preview does not create archives'
 $started=Launch $workspace @('-Headless','-NoDashboard');Check ($started.code-eq0) 'headless preview starts without opening a browser or a desktop window'
 $readyPath=Join-Path $workspace data/runtime/ready.json;Await {Test-Path -LiteralPath $readyPath} 'ready file';$ready=Get-Content -LiteralPath $readyPath -Raw|ConvertFrom-Json
 $script:ownedProcess=Get-Process -Id $ready.pid -ErrorAction Stop;$null=$script:ownedProcess.Handle;Check ([IO.Path]::GetFullPath($ownedProcess.Path)-eq[IO.Path]::GetFullPath((Join-Path $runtime hibernation-agent.exe))) 'ready PID belongs to this test private runtime'
 $encrypted=[IO.File]::ReadAllBytes((Join-Path $workspace data/runtime/.ipc-token.dpapi));$token=[Text.Encoding]::UTF8.GetString([Security.Cryptography.ProtectedData]::Unprotect($encrypted,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser));$script:headers=@{Authorization='Bearer '+$token};$script:origin='http://127.0.0.1:'+$ready.port
 $dashboard=Api '/api/dashboard';Check ($dashboard.selected_profile-eq'mock'-and$dashboard.model_budget.used-eq0-and@($dashboard.plugins|Where-Object enabled).Count-eq1-and@($dashboard.plugins|Where-Object {$_.id-eq'pixel-canvas'-and$_.enabled}).Count-eq1) 'running preview remains offline with zero provider calls and only the pixel plugin enabled'
 Check ((Api '/api/capabilities').plugins[0].current.surface.desktop-eq$false-and(SameBytes $preserved (ConfigurationBytes))) 'headless launch preserves settings and gives the plugin no desktop surface'
 $again=Launch $workspace @('-Headless','-NoDashboard');$secondReady=Get-Content -LiteralPath $readyPath -Raw|ConvertFrom-Json;Check ($again.code-eq0-and$secondReady.pid-eq$ready.pid) 'starting the same preview again reuses its own running service'
 $stopped=Launch $workspace @('-Stop');Await {$script:ownedProcess.Refresh();$script:ownedProcess.HasExited} 'preview shutdown';Check ($stopped.code-eq0-and$ownedProcess.ExitCode-eq0) 'launcher stops only its authenticated private service cleanly'
 $last=Launch $workspace @('-Stop');Check ($last.code-eq0-and(SameBytes $preserved (ConfigurationBytes))) 'a repeated stop is friendly and preserves the saved preview configuration'
 @{completed=$true;checks=$checks;root=$root;api_calls=0;production_data_used=$false;junction_tested=$junction}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Pixel preview launcher passed $checks checks. Evidence: $root"
}finally{
 if(!$ownedProcess-and(Test-Path -LiteralPath $readyPath)){try{$lastReady=Get-Content -LiteralPath $readyPath -Raw|ConvertFrom-Json;$candidate=Get-Process -Id $lastReady.pid -ErrorAction Stop;if([IO.Path]::GetFullPath($candidate.Path)-eq[IO.Path]::GetFullPath((Join-Path $runtime hibernation-agent.exe))){$ownedProcess=$candidate}}catch{}}
 if($ownedProcess){$ownedProcess.Refresh();if(!$ownedProcess.HasExited-and[IO.Path]::GetFullPath($ownedProcess.Path)-eq[IO.Path]::GetFullPath((Join-Path $runtime hibernation-agent.exe))){
  if($origin-and$headers){try{$null=Invoke-RestMethod -Uri ($origin+'/api/shutdown') -Method Post -Body '{}' -ContentType 'application/json' -Headers $headers -TimeoutSec 5;$null=$ownedProcess.WaitForExit(5000)}catch{}}
  $ownedProcess.Refresh();if(!$ownedProcess.HasExited){$ownedProcess.Kill($true);$null=$ownedProcess.WaitForExit(5000)}
 }}
}
