param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';Add-Type -Path (Join-Path $PSScriptRoot PrivateDesktop.cs)
$root=Join-Path $ScratchRoot ('supplies-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:HIBERNATION_SESSION_KEY='supplies-process-session';$env:HIBERNATION_CANTEEN_FIXTURE='synthetic-canteen-provider';$env:TEMP=Join-Path $root tmp;$env:TMP=$env:TEMP;New-Item -ItemType Directory -Force $env:TEMP|Out-Null
$provider=$null;$desktop=$null;$service=$null;$checks=[Collections.Generic.List[string]]::new()
function Check($v,$name){if(!$v){throw "CHECK FAILED: $name"};$checks.Add($name);Write-Output "PASS $name"}
function Wait-For([scriptblock]$probe,$name,[int]$seconds=35){$end=[DateTime]::UtcNow.AddSeconds($seconds);while([DateTime]::UtcNow-lt$end){if(& $probe){return};if($service-and$service.HasExited){throw 'Synthetic backend exited'};Start-Sleep -Milliseconds 60};throw "TIMEOUT $name"}
function Api($path,$data=$null){$a=@{Uri=$script:origin+$path;Headers=@{Authorization='Bearer supplies-process-session'};TimeoutSec=10};if($null-ne$data){$a.Method='Post';$a.ContentType='application/json';$a.Body=[Text.Encoding]::UTF8.GetBytes(($data|ConvertTo-Json -Depth 50 -Compress))};Invoke-RestMethod @a}
function Kitchen{(Api '/api/capabilities').plugins.Where({$_.id-eq'canteen'})[0].current.current}
function World{(Api '/api/capabilities').plugins.Where({$_.id-eq'habitat'})[0].current}
function Activities{Api '/api/activities/status'}
function Plugin($name,$arguments){$null=Api '/api/plugin/test' @{tool=$name;args=$arguments};Wait-For {$script:last=Api '/api/plugin/result';$last.state-in@('succeeded','failed')} $name;return $script:last.result}
function Start-Backend{Remove-Item (Join-Path $root ready.json) -ErrorAction SilentlyContinue;$script:desktop=[PrivateDesktop]::new($Executable,(Join-Path $root config.json),(Join-Path $root ready.json),$ProjectRoot);$script:service=$desktop.Child;Wait-For {Test-Path (Join-Path $root ready.json)} ready;$script:origin='http://127.0.0.1:'+((Get-Content (Join-Path $root ready.json) -Raw|ConvertFrom-Json).port)}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(15000)-and$service.ExitCode-eq0) 'clean encrypted shutdown';$desktop.Dispose();$script:desktop=$null;$script:service=$null}
function Plan($responses){@{nonce=[Guid]::NewGuid().ToString('N');responses=@($responses|ForEach-Object {@{response=$_}})}|ConvertTo-Json -Depth 50|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8}
function Tool($name,$arguments){@{version=1;kind='call';text=$null;calls=@(@{id='fixture';tool=$name;args=$arguments})}}
function Message($text){$id=[Guid]::NewGuid().ToString('N');$null=Api '/api/message' @{request_id=$id;text=$text};Wait-For {!(Api '/api/chat').busy} 'foreground complete'}
function Feedback{$capture=Get-Content (Join-Path $root provider/last-request.json) -Raw|ConvertFrom-Json -Depth 90;$value=$capture.messages[-1].content;if($value-isnot[string]){$value=($value|Where-Object text|Select-Object -First 1).text};@(($value|ConvertFrom-Json -Depth 90).tool_results)[0]}
function Reply{@{version=1;kind='reply';text='先准备一杯茶。';calls=@();presentation=@{speech=$false;bubble=$false}}}
try{
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$root+'/provider"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root provider.log) -RedirectStandardError (Join-Path $root provider-error.log);Wait-For {Test-Path (Join-Path $root provider/port)} 'fake provider'
 $cfg=Get-Content (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins;$cfg.frontend.startupAnimation=$false;$cfg.solitudeIntervalMs=86400000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.maxStartsPerHour=1000;$cfg.maxToolRounds=4;$cfg.review=@{enabled=$false};$cfg.reflectOnActivities=$false
 $cfg.plugins=@{widget=@{enabled=$true;roam=$false;autonomousMode=$true;autonomyIdleSeconds=600};habitat=@{enabled=$true;autoSpawn=$false;autoInteract=$false;maxObjects=8;resourceSpawn=$false;lifetimeMinSeconds=120;lifetimeMaxSeconds=600;reducedMotion=$true};canteen=@{enabled=$true;automatic=$false;automaticIntervalSeconds=900;supplyIntervalSeconds=3600;shelfLifeSeconds=1800}}
 $cfg.profiles+=@{id='fixture';label='Canteen fixture';enabled=$true;protocol='chat-completions';origin=('http://127.0.0.1:'+(Get-Content (Join-Path $root provider/port)));basePath='/v1';model='fixture-model';credentialEnvironment='HIBERNATION_CANTEEN_FIXTURE';supportsStreaming=$false;supportsJsonSchema=$false;supportsJsonObject=$true;timeoutMs=10000;maxOutputTokens=2048};$cfg|ConvertTo-Json -Depth 50|Set-Content (Join-Path $root config.json) -Encoding utf8
 Start-Backend;$null=Api '/api/model' @{id='fixture'};Wait-For {(World).desktop} 'private desktop ready'

 Wait-For {(World).inventory_available} 'inventory bridge ready'
 $asset=Invoke-WebRequest -Uri ($origin+'/ui/desktop-supplies.js');Check ($asset.StatusCode-eq200-and$asset.Content.Contains('appendDesktopResources')) 'actual backend serves resource configuration script'
 $asset=Invoke-WebRequest -Uri ($origin+'/ui/desktop-supplies.css');Check ($asset.StatusCode-eq200-and$asset.Content.Contains('.resource-region')) 'actual backend serves resource configuration styles'
 Check ((World).regions.Count-eq3) 'three authored resource regions are discoverable'
 Check ((Kitchen).inventory.used-eq0-and(Kitchen).inventory.capacity-eq8) 'engine backpack starts with eight empty slots'
 $tools=@((Api '/api/capabilities').tools.tool);Check ($tools-contains'habitat.gather'-and$tools-contains'canteen.store'-and$tools-contains'canteen.eat_supply') 'model discovers high-level gathering and inventory tools'
 $bad=Plugin 'habitat.gather' @{region='missing';count=1};Check (!$bad.ok-and$bad.error.code-eq'resource_region_unknown') 'unknown region rejected without motion'
 $null=Plugin 'habitat.create' @{kind='paper_star';placement='nearby'};$star=(World).objects.Where({$_.kind-eq'paper_star'})[0]
 $bad=Plugin 'habitat.act' @{id=$star.id;action='collect'};Check (!$bad.ok-and$bad.error.code-eq'resource_not_collectible') 'decor does not silently become an ingredient'
 $null=Plugin 'habitat.create' @{kind='berry';placement='nearby'};$null=Plugin 'habitat.create' @{kind='berry';placement='nearby'}
 $sources=@((World).objects.Where({$_.kind-eq'berry'}).id)
 $g=Plugin 'habitat.gather' @{ingredient_id='berry';count=2};Check ($g.ok-and!$g.result.completed) 'collection acceptance is not an acquired item'
 Wait-For {$last=(World).last_gather;$last.id-eq$g.result.gather_id-and$last.completed} 'collect two real desktop objects' 45
 $bag=(Kitchen).inventory;Check ($bag.used-eq2-and$bag.items.Where({$_.container-eq'backpack'}).Count-eq2) 'two world objects transferred into authoritative engine bag'
 Check ((World).objects.Where({$_.kind-eq'berry'}).Count-eq0) 'collected projections disappear from world'
 $collected=@($bag.items.Where({$_.container-eq'backpack'}));Check (@($collected.Where({$sources-contains$_.source_id})).Count-eq2) 'inventory retains actual desktop source identities'
 Check ($collected[0].remaining_seconds-le600-and$collected[0].remaining_seconds-gt0) 'collected food has bounded inherited freshness'
 Plan @((Tool 'memory.archive_search' @{query='角色这次收集了2份';limit=8}),(Reply));Message '回想刚才收集食材的经历。';$remembered=Feedback;Check ($remembered.ok-and@($remembered.result.records|Where-Object {$_.text.Contains('角色这次收集了2份')}).Count-eq1) 'batch gathering creates one source-backed searchable experience'
 $identity=$collected[0].item_id;$second=$collected[1].item_id;$fresh=$collected[0].remaining_seconds
 $stored=Plugin 'canteen.store' @{};Check ($stored.ok-and(Kitchen).inventory.used-eq0) 'whole bag can be stored atomically in kitchen warehouse'
 $inWarehouse=(Kitchen).inventory.items.Where({$_.item_id-eq$identity})[0];Check ($inWarehouse.container-eq'pantry'-and$inWarehouse.remaining_seconds-le$fresh) 'storage preserves instance and cannot refresh freshness'
 $null=Plugin 'canteen.move_supply' @{item_id=$identity;destination='backpack'};$ate=Plugin 'canteen.eat_supply' @{item_id=$identity};Check ($ate.ok-and(Kitchen).inventory.items.Where({$_.item_id-eq$identity}).Count-eq0) 'one specific inventory food can be eaten directly'
 $again=Plugin 'canteen.eat_supply' @{item_id=$identity};Check (!$again.ok) 'eaten instance cannot be consumed twice'
 Plan @((Tool 'memory.archive_search' @{query='从自己的库存中吃掉';limit=8}),(Reply));Message '回想刚才从背包里吃东西的经历。';$remembered=Feedback;Check ($remembered.ok-and@($remembered.result.records|Where-Object {$_.text.Contains('从自己的库存中吃掉')}).Count-eq1) 'inventory consumption remains a searchable actual experience'
 $null=Api '/api/control' @{paused=$true};$saved=(Kitchen).inventory.items.Where({$_.item_id-eq$second})[0];Stop-Backend;Start-Backend
 $restored=(Kitchen).inventory.items.Where({$_.item_id-eq$second})[0];Check ($restored.source_id-eq$saved.source_id-and$restored.remaining_seconds-eq$saved.remaining_seconds) 'restart restores source and exact paused freshness without offline decay'
 Check ($null-eq(World).gather-and(World).objects.Where({$sources-contains$_.id}).Count-eq0) 'restart neither replays gathering nor resurrects collected objects'
 $null=Api '/api/control' @{paused=$false};Wait-For {(World).inventory_available} 'inventory bridge after restart'
 $null=Plugin 'habitat.create' @{kind='berry';placement='nearby'}
 Plan @((Tool 'activity.enqueue' @{commands=@(@{kind='gather';ingredient_id='berry';count=2},@{kind='store_supplies'})}),(Reply));Message '收一下附近的浆果，之后放进仓库。'
 Wait-For {$a=Activities;$a.queued-eq0-and$null-eq$a.active-and$a.recent.Where({$_.kind-eq'gather'}).Count-gt0} 'cached gather then store sequence' 50
 $recent=(Activities).recent;$gathered=$recent.Where({$_.kind-eq'gather'})[-1];Check ($gathered.status-eq'partial_or_interrupted'-and$gathered.gather_result.collected-eq1) 'high-level queue records partial success when second target is absent'
 Check ((Kitchen).inventory.used-eq0-and$recent.Where({$_.kind-eq'store_supplies'-and$_.status-eq'completed'}).Count-eq1) 'next command runs only after real gathering finishes'
 # Withdraw all original flour. The recipe goal now truly lacks one flour.
 $flour=(Kitchen).inventory.items.Where({$_.ingredient_id-eq'flour'-and$_.container-eq'pantry'})[0];$null=Plugin 'canteen.move_supply' @{item_id=$flour.item_id;destination='backpack'}
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 50|ConvertFrom-Json -AsHashtable;$settings.plugins.habitat.resourceSpawn=$true;$settings.plugins.habitat.regions=@(@{id='supply-test';name='测试补给区';left=.02;top=.05;right=.95;bottom=.85;resources=@('flour');capacity=1;refresh_seconds=300});$null=Api '/api/settings' @{settings=$settings}
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=(Reply);delay_ms=8500})}|ConvertTo-Json -Depth 50|Set-Content (Join-Path $root provider/plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='合成测试：交谈期间环境照常变化。'};Wait-For {(Api '/api/chat').busy} 'synthetic conversation is processing'
 Wait-For {(World).objects.Where({$_.creator-eq'region'}).Count-eq1} 'region produces food during conversation' 8
 Check ((Api '/api/chat').busy) 'environment supply progresses independently of character conversation'
 Wait-For {!(Api '/api/chat').busy} 'synthetic delayed reply completes'
 $view=Plugin 'habitat.observe' @{};$raw=$view.result.objects.Where({$_.creator-eq'region'})[0];Check (!$raw.can_eat-and$raw.available_actions-contains'collect'-and$raw.available_actions-notcontains'eat') 'model receives actual ingredient actions independent of food-shaped visual template'

 $resource=(World).objects.Where({$_.creator-eq'region'})[0];Check ($resource.region-eq'supply-test'-and$resource.provenance.ingredient_id-eq'flour') 'semantic region and ingredient identity are separate from visual template'
 $bad=Plugin 'habitat.act' @{id=$resource.id;action='eat'};Check (!$bad.ok-and$bad.error.code-eq'world_object_not_food') 'flour visual cannot make raw flour edible'
 Plan @((Tool 'activity.enqueue' @{commands=@(@{kind='gather_for_recipe';recipe_id='bread'},@{kind='canteen_cook';recipe_id='bread'})}),(Reply));Message '为一份面包备料，再开始做。'
 Wait-For {(Kitchen).job.recipe_id-eq'bread'} 'recipe gathering deposits real material before cooking' 50
 Check ((Activities).recent.Where({$_.kind-eq'gather_for_recipe'-and$_.status-eq'completed'}).Count-eq1) 'recipe goal completes from actual missing-stock collection'
 Check ((Kitchen).inventory.items.Where({$_.source_id-eq$resource.id-and$_.container-eq'pantry'}).Count-eq1) 'gathered ingredient is the kitchen material instance'
 $null=Plugin 'canteen.cancel' @{job_id=(Kitchen).job.id}
 # No resources remain; a new request ends without pretending it gathered one.
 $g=Plugin 'habitat.gather' @{ingredient_id='apple';count=1};Wait-For {(World).last_gather.id-eq$g.result.gather_id} 'empty gathering result'
 Check ((World).last_gather.status-eq'no_matching_resource'-and(World).last_gather.collected-eq0) 'empty region does not generate resources on demand'
 $null=Plugin 'habitat.create' @{kind='apple';placement='nearby'};$apple=(World).objects.Where({$_.kind-eq'apple'})[0];$g=Plugin 'habitat.gather' @{ingredient_id='apple';count=1};$null=Api '/api/control' @{paused=$true}
 Check ($null-eq(World).gather-and(World).objects.Where({$_.id-eq$apple.id}).Count-eq1) 'pause cancels future collection without losing world item'
 Stop-Backend
 $db=[IO.File]::ReadAllBytes((Join-Path $root data/archive/records.sqlite));Check (![Text.Encoding]::UTF8.GetString($db).Contains('field/')) 'inventory snapshots remain encrypted in archive'
 @{completed=$true;checks=$checks;count=$checks.Count;root=$root;productionDataUsed=$false;realPaidCalls=0}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $root acceptance.json) -Encoding utf8
}catch{try{@{error=$_.Exception.Message;kitchen=Kitchen;world=World;activities=Activities}|ConvertTo-Json -Depth 40|Set-Content (Join-Path $root failure.json) -Encoding utf8}catch{};throw}finally{if($desktop){try{$null=Api '/api/shutdown' @{};$service.WaitForExit(10000)|Out-Null}catch{};if($service-and!$service.HasExited){$service.Kill()};$desktop.Dispose()};if($provider-and!$provider.HasExited){Stop-Process -Id $provider.Id -Force}}
