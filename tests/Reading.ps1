param([Parameter(Mandatory)][string]$Executable,[Parameter(Mandatory)][string]$ProviderExecutable,[Parameter(Mandatory)][string]$ProjectRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop';$checks=0;$service=$null;$provider=$null
$root=Join-Path $ScratchRoot ('reading-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $root|Out-Null
$env:TEMP=$root;$env:TMP=$root;$env:HIBERNATION_SESSION_KEY='reading-fixture-only'
function Check($Condition,$Name){if(!$Condition){throw "CHECK FAILED: $Name"};$script:checks++;Write-Output "PASS $Name"}
function Await([scriptblock]$Probe,$Name){$end=[DateTime]::UtcNow.AddSeconds(25);while([DateTime]::UtcNow-lt$end){if(&$Probe){return};Start-Sleep -Milliseconds 50};throw "TIMEOUT $Name"}
function Api($Path,$Data=$null){$p=@{Uri=$script:origin+$Path;Headers=@{Authorization='Bearer reading-fixture-only'};TimeoutSec=12};if($null-ne$Data){$p.Method='Post';$p.ContentType='application/json';$p.Body=[Text.Encoding]::UTF8.GetBytes(($Data|ConvertTo-Json -Depth 55 -Compress))};Invoke-RestMethod @p}
function Plugin($Name,$Arguments){
 $proposal=@{version=1;kind='call';text=$null;calls=@(@{id='read';tool=$Name;args=$Arguments})};$silent=@{version=1;kind='silence';text=$null;calls=@()}
 @{nonce=[Guid]::NewGuid().ToString('N');responses=@(@{response=$proposal},@{response=$silent})}|ConvertTo-Json -Depth 40|Set-Content -LiteralPath (Join-Path $providerRoot plan.json) -Encoding utf8
 $null=Api '/api/message' @{request_id=[Guid]::NewGuid().ToString('N');text='隔离阅读工具验证'};Await {!(Api '/api/chat').busy} $Name
 $capture=Get-Content -LiteralPath (Join-Path $providerRoot last-request.json) -Raw|ConvertFrom-Json -Depth 60;($capture.messages[-1].content|ConvertFrom-Json -Depth 55).tool_results[0]
}
function Requests{(Invoke-RestMethod -Uri ($fixture+'/fixture/reading-health')).requests}
function Start-Backend{
 $script:service=Start-Process -FilePath $Executable -ArgumentList ('--config "'+$configPath+'" --ready-file "'+$ready+'"') -WindowStyle Hidden -PassThru
 Await {if($service.HasExited){throw 'Reading backend exited'};if(Test-Path -LiteralPath $ready){return (Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).pid-eq$service.Id};return $false} 'backend'
 $script:origin='http://127.0.0.1:'+((Get-Content -LiteralPath $ready -Raw|ConvertFrom-Json).port)
}
function Stop-Backend{$null=Api '/api/shutdown' @{};Check ($service.WaitForExit(10000)) 'reading host stops cleanly'}
try{
 $providerRoot=Join-Path $root provider;New-Item -ItemType Directory -Force $providerRoot|Out-Null
 @{pages=@(@{id=1;key='Blue_glow';title='蓝色冷光';excerpt='真正的 <span class="searchmatch">蓝光</span> &amp; 夜色';description='生物发光的一个参考条目'})}|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $providerRoot reading-search.json) -Encoding utf8
 $actual='这是书页上真正读取到的蓝色冷光。';@{title='蓝色冷光';lang='zh';pageid=1;revision='12345';type='standard';extract=$actual;extract_html='<script>must-not-execute</script>';thumbnail=@{source='http://must-not-download'}}|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $providerRoot reading-summary.json) -Encoding utf8
 $provider=Start-Process -FilePath $ProviderExecutable -ArgumentList ('"'+$providerRoot+'"') -WindowStyle Hidden -PassThru;Await {Test-Path -LiteralPath (Join-Path $providerRoot port)} 'provider';$fixture='http://127.0.0.1:'+(Get-Content -LiteralPath (Join-Path $providerRoot port))
 $cfg=Get-Content -LiteralPath (Join-Path $ProjectRoot config/example.json) -Raw|ConvertFrom-Json -AsHashtable;$cfg.dataRoot=Join-Path $root data;$cfg.characterRoot=Join-Path $ProjectRoot character;$cfg.uiRoot=Join-Path $ProjectRoot ui;$cfg.pluginRoot=Join-Path (Split-Path $Executable) plugins
 $cfg.profiles=@($cfg.profiles[0],@{id='reading-fixture';label='Reading fixture';enabled=$true;protocol='chat-completions';origin=$fixture;model='fixture-model';supportsJsonObject=$true});$cfg.review=@{enabled=$false};$cfg.solitudeIntervalMs=604800000;$cfg.idleBeforeSolitudeMs=86400000;$cfg.plugins=@{reading=@{enabled=$true;language='zh';serviceOrigin=$fixture;maxRequestsPerHour=6;useSystemProxy=$false}}
 $configPath=Join-Path $root config.json;$ready=Join-Path $root ready.json;$cfg|ConvertTo-Json -Depth 55|Set-Content -LiteralPath $configPath -Encoding utf8;Start-Backend
 Check ((Requests)-eq0-and(Api '/api/dashboard').health-eq'ok') 'enabling reference tools does not automatically connect to the network'
 $null=Api '/api/model' @{id='reading-fixture'}
 $query='蓝光 & limit=99';$first=Plugin reading.search @{query=$query}
 Check ($first.ok-and$first.source_id-and$first.result.pages.Count-eq1-and$first.result.pages[0].excerpt-eq'真正的 蓝光 & 夜色'-and!$first.result.full_article_read) 'actual search returns bounded plain excerpts and authentic source'
 $wire=Get-Content -LiteralPath (Join-Path $providerRoot reading-last-request.json) -Raw|ConvertFrom-Json
 Check ($wire.target.Contains('%26%20limit%3D99')-and$wire.target.EndsWith('&limit=5')-and!$wire.authorization-and$wire.user_agent.StartsWith('AliyatHibernation/')) 'query is encoded as data and model credentials never accompany reference requests'
 $again=Plugin reading.search @{query=$query};Check ($again.ok-and$again.result.cache_hit-and$again.result.retrieved_ms-eq$first.result.retrieved_ms-and(Requests)-eq1) 'cache reuses original retrieval time without another request'
 $page=Plugin reading.open @{key='Blue_glow';language='en'}
 Check ($page.ok-and$page.result.text-eq$actual-and$page.result.revision-eq'12345'-and$page.result.scope-eq'article_lead_summary'-and!$page.result.full_article_read-and!$page.result.PSObject.Properties['extract_html']-and!$page.result.PSObject.Properties['thumbnail']) 'reading returns actual summary with scope and revision, never executable HTML or image fetching'
 Check ($page.result.provider-eq'configured_wiki'-and$page.result.source_url.StartsWith($fixture)-and$page.result.article_url.StartsWith($fixture+'/wiki/')) 'configured sources are not falsely attributed to Wikipedia'
 Check ($page.result.language-eq'zh'-and$page.result.requested_language-eq'en') 'actual source language stays distinct from requested language'
 $archive=Api '/api/archive' @{id=$page.source_id};Check ($archive.records[0].data.result.text-eq$actual) 'read text and provenance persist in the encrypted observation ledger'
 Set-Content -LiteralPath (Join-Path $providerRoot reading-status.txt) -Value 404;$r=Plugin reading.open @{key='Missing'};Check (!$r.ok-and$r.error.code-eq'reading_page_unavailable') 'missing page has no invented text'
 Set-Content -LiteralPath (Join-Path $providerRoot reading-status.txt) -Value 429;$r=Plugin reading.open @{key='RateLimited'};Check (!$r.ok-and$r.error.code-eq'reading_rate_limited') 'remote rate limit is separate from app model budget'
 Set-Content -LiteralPath (Join-Path $providerRoot reading-status.txt) -Value 200;Set-Content -LiteralPath (Join-Path $providerRoot reading-summary.json) -Value 'not json';$r=Plugin reading.open @{key='Malformed'};Check (!$r.ok-and$r.error.code-eq'reading_response_invalid') 'invalid service content cannot become a reading fact'
 $count=Requests;$r=Plugin reading.search @{query='   '};Check (!$r.ok-and(Requests)-eq$count) 'blank query never reaches network'
 @{pages=@()}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $providerRoot reading-search.json);$r=Plugin reading.search @{query='empty'};Check ($r.ok-and$r.result.pages.Count-eq0) 'empty search remains empty'
 $r=Plugin reading.search @{query='over-limit'};Check (!$r.ok-and$r.error.code-eq'reading_request_budget'-and(Requests)-eq6) 'separate reference request budget rejects before HTTP'
 Stop-Backend;Start-Backend;$r=Plugin reading.search @{query='after-restart'};Check (!$r.ok-and$r.error.code-eq'reading_request_budget'-and(Requests)-eq6) 'reference budget persists across real process restart'
 $remembered=(Api '/api/capabilities').plugins.Where({$_.id-eq'reading'})[0].current.last_read
 Check ($remembered.title-eq'蓝色冷光'-and$remembered.excerpt-eq$actual-and!$remembered.full_article_read) 'actual bounded reading excerpt survives restart without inventing new reading'
 $settings=(Api '/api/dashboard').settings|ConvertTo-Json -Depth 55|ConvertFrom-Json -AsHashtable;$settings.plugins.reading.maxRequestsPerHour=8;$null=Api '/api/settings' @{settings=$settings};$r=Plugin reading.search @{query='raised'};Check ($r.ok-and(Requests)-eq7) 'operator can raise reference limit without changing model quota'
 $sourceText=('正文里的真实蓝光。'*700)+'下一节：夜色里的微光。';@{title='蓝色冷光';content_model='wikitext';source=$sourceText;id=1;latest=@{id=54321};license=@{title='CC BY-SA';url='https://example.org/license'}}|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $providerRoot reading-source.json) -Encoding utf8
 $source=Plugin reading.open @{key='Blue_glow';language='en';detail='source';length=6000}
 Check ($source.ok-and$source.result.scope-eq'article_source_chunk'-and$source.result.content_model-eq'wikitext'-and$source.result.offset-eq0-and$source.result.next_offset-eq6000-and!$source.result.eof-and!$source.result.full_article_read-and(Requests)-eq8) 'source reading returns a bounded actual UTF8 body chunk using one request'
 Check ($source.result.reading_identity-eq$page.result.source_url-and$source.result.source_url.Contains('/w/rest.php/v1/page/Blue_glow')-and$source.result.revision-eq'54321') 'lead and source share article identity while preserving actual endpoint and revision'
 $next=Plugin reading.open @{key='Blue_glow';language='en';detail='source';offset=$source.result.next_offset;length=6000}
 Check ($next.ok-and$next.result.cache_hit-and$next.result.offset-eq6000-and$next.result.retrieved_ms-eq$source.result.retrieved_ms-and(Requests)-eq8) 'next source chunk reuses bounded source cache without another network request'
 $expected=[Text.Encoding]::UTF8.GetString([Text.Encoding]::UTF8.GetBytes($sourceText),6000,6000);Check ($next.result.text-eq$expected) 'source pagination has no fabricated text gaps or duplicated leading bytes'
 $archive=Api '/api/archive' @{id=$next.source_id};Check ($archive.records[0].data.result.text-eq$expected-and$archive.records[0].data.result.offset-eq6000-and$archive.records[0].data.result.text-ne$sourceText) 'only the actual delivered chunk is archived as an observation'
 $bad=Plugin reading.open @{key='Blue_glow';detail='lead';offset=1};Check (!$bad.ok-and$bad.error.code-eq'reading_scope_arguments'-and(Requests)-eq8) 'lead with source-only arguments fails before network'
 $bad=Plugin reading.open @{key='Blue_glow';language='en';detail='source';offset=1048576};Check (!$bad.ok-and$bad.error.code-eq'reading_offset_range'-and(Requests)-eq8) 'out of range cached source cannot become an observation'
 Stop-Backend;Start-Backend;$remembered=(Api '/api/capabilities').plugins.Where({$_.id-eq'reading'})[0].current.last_read
 Check ($remembered.scope-eq'article_source_chunk'-and$remembered.detail-eq'source'-and$remembered.offset-eq6000-and$remembered.next_offset-eq12000-and!$remembered.full_article_read) 'source range and scope survive actual process restart'
 $settings.plugins.reading.serviceOrigin='file:///C:/private';try{$null=Api '/api/settings' @{settings=$settings};throw 'invalid origin accepted'}catch{Check ($_.Exception.Response.StatusCode.value__-eq400) 'invalid service origin fails configuration atomically'}
 Check (@((Api '/api/capabilities').tools|Where-Object tool -eq reading.open).Count-eq1) 'bad settings preserve working reference tools'
 $settings.plugins.reading.serviceOrigin=$fixture;$settings.plugins.reading.enabled=$false;$null=Api '/api/settings' @{settings=$settings};Check (@((Api '/api/capabilities').tools|Where-Object {$_.tool-like'reading.*'}).Count-eq0) 'disabled reading tools leave the character catalogue'
 Stop-Backend;$raw=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes((Join-Path $cfg.dataRoot archive/records.sqlite)));Check (!$raw.Contains($actual)-and!$raw.Contains($query)) 'private reading text and query are encrypted at rest'
 @{completed=$true;checks=$checks;root=$root}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root acceptance.json);Write-Output "Reading passed: $checks checks. Evidence: $root"
}finally{foreach($process in @($service,$provider)){if($process){$process.Refresh();if(!$process.HasExited){Stop-Process -Id $process.Id}}}}
