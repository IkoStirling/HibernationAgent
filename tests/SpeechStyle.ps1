param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('speech-style-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='speech-style-session-fixture'
function Check($Value,$Name){if(!$Value){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$until=[DateTime]::UtcNow.AddSeconds(20);while([DateTime]::UtcNow-lt$until){if(&$Probe){return};Start-Sleep -Milliseconds 45};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer speech-style-session-fixture'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 70 -Compress))};Invoke-RestMethod @p}
function Calls{(Invoke-RestMethod ($fixture+'/fixture/health')).calls}
function Settings{(Api '/api/dashboard').settings|ConvertTo-Json -Depth 60|ConvertFrom-Json -AsHashtable}
function Save($Value){$null=Api '/api/settings' @{settings=$Value}}
function Message($Text){$before=Calls;$null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text=$Text};Await {!(Api '/api/chat').busy} 'turn';Check (!(Api '/api/chat').call_failed-and(Calls)-eq($before+2)) 'ordinary styled dialogue uses one actor call and one existing review call'}
. (Join-Path $PSScriptRoot ProjectedRequest.ps1)
function Check-Wire($Active,$Inactive){foreach($name in @('last-request.json','review-last-request.json')){$wire=Get-Content (Join-Path $providerRoot $name) -Raw|ConvertFrom-Json -Depth 90;$view=if($name-eq'last-request.json'){Model-CurrentEvent $wire}else{(Model-ReviewEvent $wire).event_context|ConvertFrom-Json -Depth 85};$preference=$view.expression_preference|ConvertTo-Json -Depth 30 -Compress;Check ($preference.Contains($Active)-and!$preference.Contains($Inactive)-and!$preference.Contains('presets')-and!$preference.Contains('catalog')) "$name contains only the active authoritative preference; old events remain history"}}
function Operator($Tool,$Arguments){$request=[Guid]::NewGuid().ToString('N');$null=Api '/api/plugin/test' @{request_id=$request;tool=$Tool;args=$Arguments};Await {$r=Api '/api/plugin/result';$r.request_id-eq$request-and$r.state-in@('succeeded','failed')} 'local style tool';(Api '/api/plugin/result').result}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root provider.log) -RedirectStandardError (Join-Path $root provider-error.log)
 Await {Test-Path (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $providerRoot port))
 @{version=1;kind='reply';text='窗边很安静。';calls=@()}|ConvertTo-Json -Depth 15|Set-Content (Join-Path $providerRoot response.json) -Encoding utf8
 @{nonce='approve';responses=@(@{response=@{decision='approve';reason='ok';guidance=''}})}|ConvertTo-Json -Depth 20|Set-Content (Join-Path $providerRoot review-plan.json) -Encoding utf8
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable
 $cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.computer=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000
 $cfg.profiles=@($cfg.profiles[0],@{id='actor';label='Style actor';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true;supportsStreaming=$true},@{id='review';label='Style review';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-reviewer';supportsJsonObject=$true})
 $cfg.review=@{enabled=$true;profileId='review';maxRewrites=1};$cfg.plugins=@{'speech-style'=@{enabled=$true;activePreset='alpha';allowModelSwitch=$false;presets=@(@{id='alpha';name='第一种表达';instructions='STYLE_ALPHA_ONLY_SENTINEL: Use short natural sentences.'},@{id='beta';name='第二种表达';instructions='STYLE_BETA_ONLY_SENTINEL: Use restrained hesitation.'})}}
 $cfg|ConvertTo-Json -Depth 65|Set-Content (Join-Path $root config.json) -Encoding utf8
 $service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$root+'/config.json" --ready-file "'+$root+'/ready.json"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root service.log) -RedirectStandardError (Join-Path $root error.log)
 Await {if($service.HasExited){throw 'speech-style fixture exited'};Test-Path (Join-Path $root ready.json)} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port);$null=Api '/api/model' @{id='actor'}
 Check ((Invoke-WebRequest -Uri ($script:origin+'/ui/speech-style.js') -Headers @{Authorization='Bearer speech-style-session-fixture'}).StatusCode-eq200) 'actual backend serves the preset editor asset'
 Message '窗外有什么声音？';Check-Wire 'STYLE_ALPHA_ONLY_SENTINEL' 'STYLE_BETA_ONLY_SENTINEL'
 $actorSystem=(Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 70).messages[0].content;$reviewSystem=(Get-Content (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 70).messages[0].content
 $settings=Settings;$before=Calls;$settings.plugins.'speech-style'.activePreset='beta';Save $settings;Check ((Calls)-eq$before) 'switching expression preference is local and requires no model call'
 Message '再说一句。';Check-Wire 'STYLE_BETA_ONLY_SENTINEL' 'STYLE_ALPHA_ONLY_SENTINEL'
 $switchedActor=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 70;$switchedReview=Get-Content (Join-Path $providerRoot review-last-request.json) -Raw|ConvertFrom-Json -Depth 70
 Check ($switchedActor.messages[0].content-ceq$actorSystem-and$switchedReview.messages[0].content-ceq$reviewSystem) 'switching preference preserves identical actor and reviewer system prefixes'
 $settings=Settings;$before=Calls;$settings.plugins.'speech-style'.activePreset='missing';$rejected=$false;try{Save $settings}catch{$rejected=$true};Check ($rejected-and(Calls)-eq$before) 'invalid preset configuration is rejected without calling provider'
 $settings=Settings;Check ($settings.plugins.'speech-style'.activePreset-eq'beta') 'invalid live configuration preserves previous active preset'
 Message '保持刚才的表达。';Check-Wire 'STYLE_BETA_ONLY_SENTINEL' 'STYLE_ALPHA_ONLY_SENTINEL'
 $before=Calls;$observe=Operator 'speech_style.observe' @{};Check ($observe.ok-and(Calls)-eq$before-and($observe|ConvertTo-Json -Depth 30 -Compress).Contains('STYLE_BETA_ONLY_SENTINEL')) 'style observation is a local tool'
 $settings=Settings;$settings.plugins.'speech-style'.allowModelSwitch=$true;Save $settings;$before=Calls;$selected=Operator 'speech_style.select' @{presetId='alpha'};Check ($selected.ok-and(Calls)-eq$before) 'opted-in model selection uses existing local tool mechanism'
 Message '现在呢？';Check-Wire 'STYLE_ALPHA_ONLY_SENTINEL' 'STYLE_BETA_ONLY_SENTINEL'
 $settings=Settings;$settings.plugins.'speech-style'.enabled=$false;Save $settings;Message '继续普通交谈。'
 foreach($name in @('last-request.json','review-last-request.json')){$wire=Get-Content (Join-Path $providerRoot $name) -Raw;Check (!$wire.Contains('STYLE_ALPHA_ONLY_SENTINEL')-and!$wire.Contains('STYLE_BETA_ONLY_SENTINEL')) "$name removes disabled preference instructions"}
 $null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'backend stops cleanly'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Speech style integration passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
