param([switch]$Deploy)
# Dated local release coordinator. Default is read-only preflight; -Deploy needs
# this round's frozen selection/dependency evidence and an already stopped lab.
# It never starts the production role or migrates/decrypts its archive.
$ErrorActionPreference='Stop'
if($Deploy-and[DateTime]::UtcNow-ge[DateTime]::Parse('2026-10-28T00:37:47Z').ToUniversalTime()){throw 'dated_release_window_expired'}
$projectRoot='D:\Aliyat\HibernationAgent'
$candidateRoot=Join-Path $projectRoot 'out\skills-release'
$installRoot=Join-Path $projectRoot 'out\build'
$dataRoot='E:\Aliyat\Archives\HibernationAgent'
$backupRoot='E:\Aliyat\Backups\HibernationAgent'
$configPath=Join-Path $projectRoot 'config\local.json'
$characterRoot=Join-Path $projectRoot 'character'
$selection=Get-Content -LiteralPath (Join-Path $projectRoot 'artifacts\personality-release-selection.json') -Raw|ConvertFrom-Json -AsHashtable
$serviceLock=$null;$backupPath='';$overwritten=[Collections.Generic.List[string]]::new()
$state=@{version=1;completed=$false;started_utc=[DateTime]::UtcNow.ToString('O');production_started=$false;installed_smoke=@{}}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
function Bound-File([string]$Root,[string]$Relative){
 if([string]::IsNullOrWhiteSpace($Relative)-or[IO.Path]::IsPathRooted($Relative)){throw 'absolute_or_empty_manifest_path'}
 $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\';$path=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
 if(!$path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'manifest_path_escaped_root'}
 $parent=$path
 while($parent.Length-ge$prefix.Length){
  if(Test-Path -LiteralPath $parent){if((Get-Item -LiteralPath $parent -Force).Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'reparse_point_in_copy_path'}}
  $parent=Split-Path $parent -Parent
 }
 $path
}
function Snapshot([string]$Root,[string]$Omit=''){
 $result=@{}
 foreach($item in Get-ChildItem -LiteralPath $Root -Recurse -Force){
  if($item.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'reparse_point_in_preserved_tree'}
  if($item.PSIsContainer-or$item.FullName-eq$Omit){continue}
  $relative=[IO.Path]::GetRelativePath($Root,$item.FullName);$null=Bound-File $Root $relative
  $result[$relative]=@{bytes=$item.Length;sha256=Hash $item.FullName}
 }
 $result
}
function Same-Snapshot($Left,$Right){
 if($Left.Count-ne$Right.Count){return $false}
 foreach($key in $Left.Keys){if(!$Right.ContainsKey($key)-or$Left[$key].sha256-ne$Right[$key].sha256){return $false}}
 $true
}
function Copy-Snapshot([string]$Source,[string]$Destination,$Snapshot){
 foreach($relative in $Snapshot.Keys){
  $from=Bound-File $Source $relative;$to=Bound-File $Destination $relative
  $null=[IO.Directory]::CreateDirectory((Split-Path $to -Parent));[IO.File]::Copy($from,$to,$false)
  if((Hash $to)-ne$Snapshot[$relative].sha256){throw 'backup_copy_hash_mismatch'}
 }
}
function Installed-Stopped{
 $path=Join-Path $installRoot 'hibernation-agent.exe'
 @((Get-CimInstance Win32_Process -Filter "Name='hibernation-agent.exe'")|Where-Object ExecutablePath -eq $path).Count-eq0
}
function Installed-Test([string]$Script,[string[]]$Arguments,[string]$Log){
 $pwshExecutable=Join-Path $PSHOME 'pwsh.exe'
 & $pwshExecutable -NoProfile -File (Join-Path $projectRoot ('tests\'+$Script)) @Arguments *> (Join-Path $projectRoot ('artifacts\'+$Log))
 if($LASTEXITCODE-ne0){throw 'installed_fixture_failed'}
 $state.installed_smoke[$Script]='passed'
}
foreach($root in @($candidateRoot,$installRoot,$dataRoot,$backupRoot,$characterRoot,$configPath)){
 $ancestor=[IO.Path]::GetFullPath($root)
 while($ancestor){
  if((Test-Path -LiteralPath $ancestor)-and((Get-Item -LiteralPath $ancestor -Force).Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'reparse_ancestor_in_release_scope'}
  $ancestor=Split-Path $ancestor -Parent
 }
}
if($selection.version-ne2-or$selection.candidate-ne$candidateRoot-or$selection.package-ne$installRoot-or!$selection.preserve_unselected_files){throw 'unexpected_selection'}
if($selection.expected_host_sha256-ne'FFADBC7AD69034B1B90F7966B29FC1829EB4C9CB23C590A3FA5269C4A85E2944'){throw 'unverified_host_identity'}
if((Hash (Join-Path $candidateRoot 'hibernation-agent.exe'))-ne$selection.expected_host_sha256){throw 'candidate_host_changed'}
$cfg=Get-Content -LiteralPath $configPath -Raw|ConvertFrom-Json
if([IO.Path]::GetFullPath($cfg.dataRoot)-ne$dataRoot){throw 'unexpected_production_data_root'}
if((Get-Item -LiteralPath $candidateRoot).Attributes-band[IO.FileAttributes]::ReparsePoint-or(Get-Item -LiteralPath $installRoot).Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'reparse_root'}
foreach($row in $selection.files){
 $from=Bound-File $candidateRoot $row.path;$null=Bound-File $installRoot $row.path
 if((Hash $from)-ne$row.sha256){throw 'candidate_file_changed'}
}
$assessment=Get-Content -LiteralPath (Join-Path $projectRoot 'artifacts\personality-native-dependency-assessment.json') -Raw|ConvertFrom-Json
if(!$assessment.local_runtime_dependency_check_passed){throw 'dependency_check_not_passed'}
& (Join-Path $candidateRoot 'hibernation-agent.exe') --config $configPath --check-config
if($LASTEXITCODE-ne0){throw 'candidate_configuration_invalid'}
if(!(Installed-Stopped)){throw 'production_is_running_stop_and_preserve_its_state_first'}
$lockPath=Join-Path $dataRoot 'runtime\service.lock'
try{
 $serviceLock=[IO.File]::Open($lockPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 $dataBefore=Snapshot $dataRoot $lockPath;$characterBefore=Snapshot $characterRoot;$configBefore=Hash $configPath
 $runtimeBefore=@{}
 foreach($row in $selection.files){$path=Bound-File $installRoot $row.path;if(Test-Path -LiteralPath $path){if((Get-Item -LiteralPath $path).PSIsContainer){throw 'installed_target_is_directory'};$runtimeBefore[$row.path]=@{bytes=(Get-Item -LiteralPath $path).Length;sha256=Hash $path}}}
 $required=($dataBefore.Values|Measure-Object bytes -Sum).Sum+($runtimeBefore.Values|Measure-Object bytes -Sum).Sum+($characterBefore.Values|Measure-Object bytes -Sum).Sum+(Get-Item -LiteralPath $configPath).Length
 $available=(Get-Volume -DriveLetter E).SizeRemaining
 if($available-lt$required+268435456){throw 'backup_space_insufficient'}
 if(!$Deploy){
  @{mode='prepared_only';selected_files=$selection.files.Count;backup_bytes=$required;available_bytes=$available;formal_data_files=$dataBefore.Count;installed_state='stopped';candidate_sha256=$selection.expected_host_sha256}|ConvertTo-Json -Compress
  return
 }
 $lab=Get-Content -LiteralPath (Join-Path $projectRoot 'artifacts\personality-growth-20261027\controller.json') -Raw|ConvertFrom-Json
 if($lab.running-or(Get-Process -Id $lab.pid,$lab.backend_pid -ErrorAction SilentlyContinue)-or(Test-Path -LiteralPath (Join-Path $projectRoot 'artifacts\personality-growth-20261027\budget.json.lock'))-or(Test-Path -LiteralPath (Join-Path $projectRoot 'artifacts\personality-growth-20261027\runner.lock'))){throw 'paid_lab_has_not_stopped'}
 $backupPath=Join-Path $backupRoot ('personality-deployment-'+(Get-Date -Format yyyyMMdd-HHmmss)+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
 $null=[IO.Directory]::CreateDirectory($backupPath)
 Copy-Snapshot $dataRoot (Join-Path $backupPath 'data') $dataBefore
 Copy-Snapshot $characterRoot (Join-Path $backupPath 'character') $characterBefore
 Copy-Snapshot $installRoot (Join-Path $backupPath 'runtime') $runtimeBefore
 [IO.File]::Copy($configPath,(Join-Path $backupPath 'local.json'),$false)
 if((Hash (Join-Path $backupPath 'local.json'))-ne$configBefore-or(Hash $configPath)-ne$configBefore-or!(Same-Snapshot $dataBefore (Snapshot $dataRoot $lockPath))-or!(Same-Snapshot $characterBefore (Snapshot $characterRoot))){throw 'source_changed_during_backup'}
 $backupManifest=@{version=1;completed=$true;data=$dataBefore;runtime=$runtimeBefore;character=$characterBefore;config_sha256=$configBefore;selection=$selection;excluded_ephemeral_lock=$lockPath;created_utc=[DateTime]::UtcNow.ToString('O')}
 $backupManifest|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $backupPath 'backup.json') -Encoding utf8
 foreach($row in $selection.files){
  $from=Bound-File $candidateRoot $row.path;$to=Bound-File $installRoot $row.path
  if((Hash $from)-ne$row.sha256){throw 'candidate_changed_before_copy'}
  if($runtimeBefore.ContainsKey($row.path)){if((Hash $to)-ne$runtimeBefore[$row.path].sha256){throw 'installed_file_changed_after_backup'}}elseif(Test-Path -LiteralPath $to){throw 'new_installed_file_appeared_after_backup'}
  $null=[IO.Directory]::CreateDirectory((Split-Path $to -Parent));$overwritten.Add($row.path);[IO.File]::Copy($from,$to,$true)
  if((Hash $to)-ne$row.sha256){throw 'installed_copy_hash_mismatch'}
 }
 & (Join-Path $installRoot 'hibernation-agent.exe') --config $configPath --check-config
 if($LASTEXITCODE-ne0){throw 'installed_configuration_invalid'}
 $scratch=Join-Path $projectRoot 'artifacts\personality-installed-validation'
 Installed-Test 'Integration.ps1' @('-Executable',(Join-Path $installRoot 'hibernation-agent.exe'),'-ProviderExecutable',(Join-Path $candidateRoot 'Hibernation_TestProvider.exe'),'-ProjectRoot',$projectRoot,'-ScratchRoot',$scratch) 'personality-installed-process.log'
 Installed-Test 'ConfigurationCheck.ps1' @('-Executable',(Join-Path $installRoot 'hibernation-agent.exe'),'-ProjectRoot',$projectRoot,'-ScratchRoot',$scratch) 'personality-installed-configuration.log'
 Installed-Test 'RuntimeTools.ps1' @('-ToolsRoot',(Join-Path $installRoot 'tools'),'-ScratchRoot',$scratch) 'personality-installed-tools.log'
 foreach($row in $selection.files){if((Hash (Bound-File $installRoot $row.path))-ne$row.sha256){throw 'installed_files_changed_during_smoke'}}
 if((Hash $configPath)-ne$configBefore-or!(Same-Snapshot $dataBefore (Snapshot $dataRoot $lockPath))-or!(Same-Snapshot $characterBefore (Snapshot $characterRoot))){throw 'preserved_data_changed'}
 if(!(Installed-Stopped)){throw 'unexpected_production_process'}
 $state.completed=$true;$state.backup=$backupPath;$state.backup_bytes=$required;$state.installed_files=$selection.files.Count;$state.host_sha256=$selection.expected_host_sha256;$state.validated_feature_commit=$selection.validated_feature_commit;$state.formal_data_files_unchanged=$dataBefore.Count;$state.configuration_unchanged=$true;$state.character_unchanged=$true;$state.actual_model_calls=0;$state.local_runtime_only=$true;$state.completed_utc=[DateTime]::UtcNow.ToString('O')
 $state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $backupPath 'deployment.json') -Encoding utf8
 $state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $projectRoot 'artifacts\personality-deployment.json') -Encoding utf8
 $state|ConvertTo-Json -Depth 5
}catch{
 $state.failure_type=$_.Exception.GetType().Name;$state.backup=$backupPath;$state.rollback_completed=$false
 if($overwritten.Count){
  try{
   foreach($relative in $overwritten){$to=Bound-File $installRoot $relative;if($runtimeBefore.ContainsKey($relative)){[IO.File]::Copy((Bound-File (Join-Path $backupPath 'runtime') $relative),$to,$true);if((Hash $to)-ne$runtimeBefore[$relative].sha256){throw 'rollback_hash_mismatch'}}elseif(Test-Path -LiteralPath $to){[IO.File]::Delete($to)}}
   $state.rollback_completed=$true
  }catch{$state.rollback_completed=$false}
 }
 if($backupPath){$state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $backupPath 'deployment-failure.json') -Encoding utf8}
 throw 'Deployment stopped; inspect the local backup outcome before retrying.'
}finally{if($serviceLock){$serviceLock.Dispose()}}
