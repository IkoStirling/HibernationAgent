# Paths resolve against the checkout, never the caller's current directory.
function Resolve-HibernationPath([string]$Explicit,[string]$EnvironmentName,[string]$Default,[string]$ProjectRoot){
 $value=if(![string]::IsNullOrWhiteSpace($Explicit)){$Explicit}else{[Environment]::GetEnvironmentVariable($EnvironmentName,'Process')}
 if([string]::IsNullOrWhiteSpace($value)){$value=$Default}
 if([IO.Path]::IsPathRooted($value)){[IO.Path]::GetFullPath($value)}else{[IO.Path]::GetFullPath((Join-Path $ProjectRoot $value))}
}
function Resolve-HibernationBuildRoot([string]$Explicit,[string]$ProjectRoot){Resolve-HibernationPath $Explicit 'HIBERNATION_BUILD_ROOT' 'out/build' $ProjectRoot}
function Resolve-HibernationRuntimeRoot([string]$Explicit,[string]$ProjectRoot){
 if(!$Explicit-and!$env:HIBERNATION_BUILD_ROOT){$pointer=Join-Path $ProjectRoot out/cognitive-runtime-current.json;if(Test-Path -LiteralPath $pointer){$current=Get-Content -LiteralPath $pointer -Raw|ConvertFrom-Json;if($current.build_root-and(Test-Path -LiteralPath (Join-Path $current.build_root hibernation-agent.exe))){return [IO.Path]::GetFullPath($current.build_root)}}}
 Resolve-HibernationBuildRoot $Explicit $ProjectRoot
}
