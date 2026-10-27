param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$service=$null;$provider=$null;$checks=0
$root=Join-Path $ScratchRoot ('skills-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $root -Force|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='skills-fixture-only'
function Check($value,$name){if(!$value){throw "CHECK FAILED: $name"};$script:checks++;Write-Output "PASS $name"}
function Await([scriptblock]$probe,$name){$end=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$end){if(&$probe){return};Start-Sleep -Milliseconds 35};throw "TIMEOUT $name"}
function Api($path,$data=$null){$p=@{Uri=$script:origin+$path;Headers=@{Authorization='Bearer skills-fixture-only'};TimeoutSec=10};if($null-ne$data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($data|ConvertTo-Json -Depth 60 -Compress))};Invoke-RestMethod @p}
function Plan($responses,$review=$false){$file=if($review){'review-plan.json'}else{'plan.json'};@{nonce=[guid]::NewGuid().ToString('N');responses=@($responses|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 60|Set-Content -LiteralPath (Join-Path $providerRoot $file) -Encoding utf8}
function Call($id,$tool,$arguments){@{id=$id;tool=$tool;args=$arguments}}
function Calls($items){@{version=1;kind='call';text=$null;calls=@($items)}}
function Reply{@{version=1;kind='reply';text='这页已经核对过了。';calls=@();presentation=@{speech=$false;bubble=$false}}}
function Native($id,$name,$arguments){@{fixture_native_calls=@(@{id=$id;name=$name;arguments=$arguments})}}
function Message{$null=Api '/api/message' @{request_id=[guid]::NewGuid().ToString('N');text='本地隔离Skill测试'};Await {!(Api '/api/chat').busy} 'turn';$job=@((Api '/api/state').jobs|Where-Object id -eq 'chat')[0];$info=$job.result|ConvertFrom-Json -Depth 60;Api '/api/trace' @{id=$info.trace_id}}
function Settings{(Api '/api/dashboard').settings|ConvertTo-Json -Depth 60|ConvertFrom-Json -AsHashtable}
function Save($s){$null=Api '/api/settings' @{settings=$s}}
function Start-Backend{$script:service=Start-Process $Executable -ArgumentList ('--config "'+$config+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root service.log) -RedirectStandardError (Join-Path $root error.log);Await {if($service.HasExited){throw 'backend exited'};if(Test-Path $ready){return (Get-Content $ready -Raw|ConvertFrom-Json).pid-eq$service.Id}} 'backend';$script:origin='http://127.0.0.1:'+((Get-Content $ready -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)-and$service.ExitCode-eq0) 'graceful shutdown'}
try{
 $providerRoot=Join-Path $root provider;$provider=Start-Process $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru
 Await {Test-Path (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content (Join-Path $providerRoot port))
 $pack=Join-Path $root skills/notes;New-Item -ItemType Directory -Path (Join-Path $pack references),(Join-Path $pack scripts) -Force|Out-Null
 [IO.File]::WriteAllText((Join-Path $pack SKILL.md),"---`nname: Notes`ndescription: A bounded local procedure`n---`nRead the actual draft before changing it.`n")
 [IO.File]::WriteAllText((Join-Path $pack references/example.txt),'Reference only.')
 [IO.File]::WriteAllText((Join-Path $pack scripts/forbidden.ps1),"throw 'this script must never run'")
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.plugins=@{};$cfg.computer=@{enabled=$true;readRoots=@()};$cfg.skills=@{enabled=$true;root=(Join-Path $root skills);useBuiltins=$false;grants=@()};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000;$cfg.maxToolRounds=8
 $cfg.profiles=@($cfg.profiles[0]);foreach($protocol in @('json','chat-completions','responses')){$cfg.profiles+=@{id=$protocol;label=$protocol;protocol=if($protocol-eq'json'){'chat-completions'}else{$protocol};enabled=$true;origin=$fixture;model='fixture-model';supportsToolCalls=($protocol-ne'json');supportsStreaming=$true}}
 $cfg.profiles+=@{id='review';label='Review';protocol='chat-completions';enabled=$true;origin=$fixture;model='fixture-reviewer'};$cfg.review=@{enabled=$true;profileId='review';maxRewrites=1}
 $config=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 60|Set-Content $config -Encoding utf8
 Plan @(@{decision='approve';reason='ok';guidance=''}) $true;Start-Backend;$null=Api '/api/model' @{id='json'}
 $package=(Api '/api/skills').packages[0];Check (!$package.approved-and$package.digest.Length-eq64) 'new local package requires independent approval'
 Plan @((Calls @((Call 'read' 'skills.read' @{id='notes'}))),(Reply));$trace=Message;Check (!$trace.tool_results[0].ok) 'model cannot read an unapproved guide'
 $s=Settings;$s.skills.grants=@(@{id='notes';digest=$package.digest;tools=@('workspace.list','workspace.read','workspace.write','workspace.search','workspace.edit')});Save $s
 [IO.File]::WriteAllText((Join-Path $cfg.dataRoot drafts/note.txt),'alpha old line');New-Item -ItemType Directory (Join-Path $cfg.dataRoot materials/sub) -Force|Out-Null;[IO.File]::WriteAllText((Join-Path $cfg.dataRoot materials/sub/source.txt),'The blue lantern is here.')
 $hash=(Get-FileHash (Join-Path $cfg.dataRoot drafts/note.txt)).Hash.ToLowerInvariant()
 Plan @((Calls @((Call 'activate' 'skills.activate' @{id='notes';digest=$package.digest}),(Call 'read' 'workspace.read' @{mount='drafts';path='note.txt'}))),(Calls @((Call 'search' 'workspace.search' @{mount='materials';query='blue lantern'}),(Call 'edit' 'workspace.edit' @{path='note.txt';expected_sha256=$hash;old_text='old';new_text='new'}),(Call 'denied' 'computer.list_directory' @{root='materials'}))),(Reply));$trace=Message
 Check ((Get-Content (Join-Path $cfg.dataRoot drafts/note.txt) -Raw)-eq'alpha new line') 'approved guide can perform actual scoped draft editing'
 $capture=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 70;$feedback=($capture.messages[-1].content|ConvertFrom-Json -Depth 60).tool_results;Check ($feedback[0].result.matches.Count-eq1) 'bounded search finds real content in an authorized subdirectory'
 Check (@($trace.tool_results|Where-Object {$_.error_code-eq'skill_tool_denied'}).Count-eq1) 'active Skill cannot use a non-granted application tool'
 Plan @((Calls @((Call 'stale' 'workspace.edit' @{path='note.txt';expected_sha256=$hash;old_text='new';new_text='lost'}),(Call 'escape' 'workspace.search' @{mount='drafts';path='../archive';query='private'}),(Call 'script' 'skills.read' @{id='notes';path='scripts/forbidden.ps1'}),(Call 'resource' 'skills.read' @{id='notes';path='references/example.txt'}))),(Reply));$trace=Message
 Check ($trace.tool_results[0].error_code-eq'workspace_revision_conflict') 'stale content digest cannot overwrite a changed draft'
 Check (!$trace.tool_results[1].ok-and!$trace.tool_results[2].ok-and$trace.tool_results[3].ok) 'path escape and executable resource are denied while approved reference text is readable'
 $editHash=(Get-FileHash (Join-Path $cfg.dataRoot drafts/note.txt)).Hash.ToLowerInvariant();$edit=@{path='note.txt';expected_sha256=$editHash;old_text='new';new_text='reviewed'}
 Plan @(@{decision='approve';reason='ok';guidance=''},@{decision='rewrite';reason='meta_leak';guidance='不要重复已经执行的编辑，使用真实回执。'},@{decision='approve';reason='ok';guidance=''}) $true
 Plan @((Calls @((Call 'edit-once' 'workspace.edit' $edit))),(Reply),(Calls @((Call 'edit-repeat' 'workspace.edit' $edit))),(Reply));$trace=Message
 Check ($trace.tool_results[-1].ok-and$trace.tool_results[-1].reused_after_review-and(Get-Content (Join-Path $cfg.dataRoot drafts/note.txt) -Raw)-eq'alpha reviewed line') 'review rewrite reuses actual edit receipt rather than applying the patch twice'
 Plan @(@{decision='approve';reason='ok';guidance=''}) $true
 $s=Settings;$s.skills.grants[0].tools=@('workspace.read');Save $s
 Plan @((Calls @((Call 'same' 'workspace.write' @{path='once.txt';content='once'}))),(Calls @((Call 'activate' 'skills.activate' @{id='notes';digest=$package.digest}))),(Calls @((Call 'same' 'workspace.write' @{path='once.txt';content='once'}))),(Reply));$trace=Message
 Check ($trace.tool_results[-1].error_code-eq'skill_tool_denied') 'permission is checked before reusing a previously successful operation receipt'
 foreach($protocol in @('chat-completions','responses')){
  $null=Api '/api/model' @{id=$protocol};Plan @((Native 'activate' 'ha_skills_DOT_activate' @{id='notes';digest=$package.digest}),(Native 'read' 'ha_workspace_DOT_read' @{mount='drafts';path='note.txt'}),(Native 'final' 'ha_turn_reply' @{text='这页读过了。'}));$trace=Message
  Check ($trace.tool_results.Count-eq2-and$trace.tool_results[1].ok-and!(Api '/api/chat').call_failed) "$protocol standard tool calls execute through Skill and application checks"
  $capture=Get-Content (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 70;$names=@($capture.tools|ForEach-Object {if($_.function){$_.function.name}else{$_.name}})
  Check ($names-contains'ha_workspace_DOT_read'-and$names-notcontains'ha_workspace_DOT_write') "$protocol only advertises the active permission intersection"
 }
 $null=Api '/api/model' @{id='json'};$legacy=@{visible_reply='';actions=@(@{kind='write_draft';path='legacy.txt';content='forbidden'});memories=@();state_update=@{};stage_proposal=$null}
 Plan @((Calls @((Call 'activate' 'skills.activate' @{id='notes';digest=$package.digest}))),$legacy);$trace=Message
 Check ($trace.call_error.code-eq'skill_legacy_effect_denied'-and!(Test-Path (Join-Path $cfg.dataRoot drafts/legacy.txt))) 'legacy structured output cannot bypass the active Skill gate'
 $s=Settings;$s.skills.root=Join-Path $cfg.dataRoot drafts;try{Save $s;throw 'overlapping root accepted'}catch{Check ($_.Exception.Response.StatusCode.value__-eq400) 'Skill instructions cannot be placed in model-writable drafts'}
 [IO.File]::AppendAllText((Join-Path $pack SKILL.md),"`nChanged after approval.");Save (Settings);$changed=(Api '/api/skills').packages[0]
 Check (!$changed.approved-and$changed.issue-eq'content_changed') 'changed package requires a new content-bound approval on reload'
 $linked=Join-Path $root skills/zz-link-probe;New-Item -ItemType Directory -Path $linked,(Join-Path $root outside-pack) -Force|Out-Null;[IO.File]::WriteAllText((Join-Path $linked SKILL.md),'Do not follow external references.');New-Item -ItemType Junction -Path (Join-Path $linked linked) -Target (Join-Path $root outside-pack)|Out-Null;Save (Settings)
 Check (@((Api '/api/skills').packages|Where-Object {$_.id-eq'zz-link-probe'-and$_.issue-eq'invalid_or_unsafe_package'}).Count-eq1) 'Windows junctions inside a Skill package are rejected before reading linked contents'
 $node=if($env:HA_NODE_RUNTIME){$env:HA_NODE_RUNTIME}else{'C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe'}; & $node (Join-Path $ProjectRoot tests/SkillsBrowser.mjs) $script:origin;if($LASTEXITCODE-ne0){throw 'Skill dashboard browser checks failed'}
 Stop-Backend;Start-Backend;Check (!(Api '/api/skills').packages[0].approved) 'approval revocation survives process restart'
 Stop-Backend;Check (!(Get-Content (Join-Path $root service.log) -Raw).Contains('skills-fixture-only')) 'session credential is absent from service logs'
 @{completed=$true;checks=$checks;production_archive_read=$false}|ConvertTo-Json|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
 Write-Output "Skills integration passed: $checks checks. Evidence: $root"
}finally{if($service-and!$service.HasExited){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(5000)|Out-Null}catch{};if(!$service.HasExited){Stop-Process -Id $service.Id}};if($provider-and!$provider.HasExited){Stop-Process -Id $provider.Id}}
