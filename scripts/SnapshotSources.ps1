param(
 [string]$ProjectRoot=(Split-Path $PSScriptRoot -Parent),
 [string]$OutputRoot='E:/Aliyat/Backups/HibernationAgent'
)
$ErrorActionPreference='Stop'
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot)
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
$sourceRoot=$ProjectRoot+[IO.Path]::DirectorySeparatorChar
$extensions=@('.cpp','.h','.inc','.json','.md','.ps1','.py','.js','.mjs','.html','.css','.txt','.png','.svg','.cs','.cmake','.cmd')
$bundledToolsRoot=[IO.Path]::GetFullPath((Join-Path $ProjectRoot tools/runtime))+[IO.Path]::DirectorySeparatorChar
function Source-Files {
 $items=@(foreach($directory in @('src','include','plugins','ui','scripts','tools','tests','docs','character','cmake','design')){
  Get-ChildItem -LiteralPath (Join-Path $ProjectRoot $directory) -File -Recurse|
   Where-Object {
    $bundled=$_.FullName.StartsWith($bundledToolsRoot,[StringComparison]::OrdinalIgnoreCase)
    ($bundled-or$_.Extension-in$extensions)-and$_.Name-ne'local.json'-and!($_.Attributes-band[IO.FileAttributes]::ReparsePoint)-and
    $_.FullName-notmatch'[\\/](CMakeFiles|data|out|artifacts|secrets|\.git)[\\/]'-and
    ($bundled-or$_.FullName-notmatch'[\\/]runtime[\\/]')
   }
 })
 foreach($name in @('CMakeLists.txt','README.md','AGENTS.md','.gitignore','.gitattributes','engine-version.json','Start.cmd','Stop.cmd','config/example.json')){$items+=Get-Item -LiteralPath (Join-Path $ProjectRoot $name)}
 @($items|Sort-Object FullName -Unique)
}
function Fingerprints {
 $values=@{};foreach($file in Source-Files){
  $absolute=[IO.Path]::GetFullPath($file.FullName)
  if(!$absolute.StartsWith($sourceRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Source escaped the project root'}
  $relative=[IO.Path]::GetRelativePath($ProjectRoot,$absolute)
  $values[$relative]=(Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash
 };$values
}
$fingerprints=Fingerprints
$name='source-'+(Get-Date -Format yyyyMMdd-HHmmss)+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$destination=Join-Path $OutputRoot $name;$filesRoot=Join-Path $destination source
New-Item -ItemType Directory -Path $filesRoot -Force|Out-Null
foreach($relative in $fingerprints.Keys){
 $target=Join-Path $filesRoot $relative;New-Item -ItemType Directory -Path (Split-Path $target) -Force|Out-Null
 Copy-Item -LiteralPath (Join-Path $ProjectRoot $relative) -Destination $target
 if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash-ne$fingerprints[$relative]){throw 'Source changed while copying; incomplete snapshot retained for inspection'}
}
$after=Fingerprints
if($after.Count-ne$fingerprints.Count){throw 'Source file inventory changed during snapshot; incomplete copy was not packaged'}
foreach($relative in $fingerprints.Keys){if($after[$relative]-ne$fingerprints[$relative]){throw 'Source changed during snapshot; incomplete copy was not packaged'}}
$fingerprints|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $destination files-sha256.json) -Encoding utf8
$appHead=(& git -C $ProjectRoot rev-parse HEAD);if($LASTEXITCODE-ne0){throw 'Cannot identify application revision'}
$engineRoot=Join-Path $ProjectRoot ../AliyatEngine;$engineHead=(& git -C $engineRoot rev-parse HEAD);if($LASTEXITCODE-ne0){throw 'Cannot identify engine revision'}
$metadata=@{
 version=1;completed=$true;created_utc=[DateTime]::UtcNow.ToString('O');source_files=$fingerprints.Count
 application_head=$appHead;engine_head=$engineHead;engine_worktree_dirty=(@(& git -C $engineRoot status --porcelain).Count-gt0);shared_worktree=$true
 attribution='A local source snapshot including concurrent application changes; not an isolated commit or a distribution package.'
 included_roots=@('src','include','plugins','ui','scripts','tools','tests','docs','character','cmake','design','engine-version.json','Start.cmd','Stop.cmd','config/example.json')
 excluded_roots=@('config/local.json','config/ai-stage.pending.json','data','out','artifacts','.git','runtime')
 engine_dependencies='Pinned portable Git/7-Zip and licenses from tools/runtime are included. Engine source and installed engine dependencies are not bundled; use the recorded revision and matching local dependencies.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=Join-Path $destination source.zip
[IO.Compression.ZipFile]::CreateFromDirectory($filesRoot,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
$zipHash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
@{archive=$zip;sha256=$zipHash;source_files=$fingerprints.Count}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $destination archive-sha256.json) -Encoding utf8
$metadata|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $destination snapshot.json) -Encoding utf8
Write-Output "Source snapshot: $destination ($($fingerprints.Count) files)"
