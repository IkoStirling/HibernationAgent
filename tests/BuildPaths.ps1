param([Parameter(Mandatory)][string]$ProjectRoot)
$ErrorActionPreference='Stop';. (Join-Path $ProjectRoot scripts/BuildPaths.ps1)
$originalBuild=$env:HIBERNATION_BUILD_ROOT;$originalDeps=$env:HIBERNATION_DEPENDENCY_ROOT;$checks=0
function Check($Condition,$Name){if(!$Condition){throw $Name};$script:checks++}
try{
 Remove-Item Env:HIBERNATION_BUILD_ROOT -ErrorAction SilentlyContinue
 Check ((Resolve-HibernationBuildRoot '' $ProjectRoot)-eq[IO.Path]::GetFullPath((Join-Path $ProjectRoot out/build))) 'default checkout build'
 $env:HIBERNATION_BUILD_ROOT='Z:/机械硬盘/XiaoAi build'
 Check ((Resolve-HibernationBuildRoot '' $ProjectRoot)-eq[IO.Path]::GetFullPath($env:HIBERNATION_BUILD_ROOT)) 'external root supports spaces and unicode without creating it'
 Check ((Resolve-HibernationBuildRoot 'out/explicit' $ProjectRoot)-eq[IO.Path]::GetFullPath((Join-Path $ProjectRoot out/explicit))) 'explicit path overrides environment'
 $env:HIBERNATION_BUILD_ROOT='out/environment'
 Push-Location $env:SystemRoot
 try{Check ((Resolve-HibernationBuildRoot '' $ProjectRoot)-eq[IO.Path]::GetFullPath((Join-Path $ProjectRoot out/environment))) 'relative environment resolves against source not cwd'}finally{Pop-Location}
 $env:HIBERNATION_DEPENDENCY_ROOT='Y:/共享依赖'
 Check ((Resolve-HibernationPath '' 'HIBERNATION_DEPENDENCY_ROOT' out/deps $ProjectRoot)-eq[IO.Path]::GetFullPath($env:HIBERNATION_DEPENDENCY_ROOT)) 'dependencies independent of build and source'
 Check ((Get-Content (Join-Path $ProjectRoot scripts/NewPortablePackage.ps1) -Raw).Contains("'BuildPaths.ps1'")) 'packaged launcher includes its path helper'
 @{completed=$true;checks=$checks}|ConvertTo-Json
}finally{
 if($null-eq$originalBuild){Remove-Item Env:HIBERNATION_BUILD_ROOT -ErrorAction SilentlyContinue}else{$env:HIBERNATION_BUILD_ROOT=$originalBuild}
 if($null-eq$originalDeps){Remove-Item Env:HIBERNATION_DEPENDENCY_ROOT -ErrorAction SilentlyContinue}else{$env:HIBERNATION_DEPENDENCY_ROOT=$originalDeps}
}
