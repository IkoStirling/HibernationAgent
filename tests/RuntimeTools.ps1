param([Parameter(Mandatory)][string]$ToolsRoot,[Parameter(Mandatory)][string]$ScratchRoot)
$ErrorActionPreference='Stop'
& (Join-Path $ToolsRoot VerifyRuntimeTools.ps1) -ToolsRoot $ToolsRoot
$fixture=Join-Path ([IO.Path]::GetFullPath($ScratchRoot)) ('portable-tools-'+[guid]::NewGuid().ToString('N'))
$source=Join-Path $fixture '仓库 with spaces'
New-Item -ItemType Directory -Path $source -Force | Out-Null
# Isolate from global hooks, credentials, signing and installed Git/7-Zip.
$env:GIT_CONFIG_NOSYSTEM='1'
$env:GIT_CONFIG_GLOBAL=Join-Path $fixture empty.gitconfig
[IO.File]::WriteAllText($env:GIT_CONFIG_GLOBAL,'')
$env:GIT_TERMINAL_PROMPT='0'
$env:PATH="$env:SystemRoot\System32;$env:SystemRoot"
$git=Join-Path $ToolsRoot bin/git.cmd
$seven=Join-Path $ToolsRoot bin/7z.cmd
function Git-Run([string[]]$Arguments){
    & $git -C $source @Arguments
    if($LASTEXITCODE -ne 0){throw "Bundled Git failed: $($Arguments[0])"}
}
Git-Run @('init','-b','main')
Git-Run @('config','user.name','Portable Tools Test')
Git-Run @('config','user.email','tools-test@example.invalid')
$original='Unicode 文件 and portable tools.'
[IO.File]::WriteAllText((Join-Path $source '样例.txt'),$original)
Git-Run @('add','--','样例.txt')
Git-Run @('commit','-m','Portable tool fixture')
Git-Run @('tag','-a','fixture-v1','-m','Fixture tag')
$status=& $git -C $source status --porcelain
if($LASTEXITCODE -ne 0 -or $status){throw 'Portable Git fixture is not clean.'}
$clone=Join-Path $fixture clone
& $git clone --no-hardlinks $source $clone
if($LASTEXITCODE -ne 0){throw 'Portable local Git clone failed.'}
foreach($format in @('7z','zip')){
    $archive=Join-Path $fixture "test.$format"
    & $seven a "-t$format" $archive (Join-Path $source '样例.txt') -y -bso0 -bsp0
    if($LASTEXITCODE -ne 0){throw "Archive creation failed: $format"}
    & $seven t $archive -bso0 -bsp0
    if($LASTEXITCODE -ne 0){throw "Archive integrity test failed: $format"}
    $destination=Join-Path $fixture "restored-$format"
    & $seven x $archive "-o$destination" -y -bso0 -bsp0
    if($LASTEXITCODE -ne 0){throw "Archive extraction failed: $format"}
    if([IO.File]::ReadAllText((Join-Path $destination '样例.txt')) -cne $original){throw "Archive contents changed: $format"}
}
Write-Output 'Portable Git init/commit/tag/clone and Unicode 7z/zip round-trips passed without installed tools.'
