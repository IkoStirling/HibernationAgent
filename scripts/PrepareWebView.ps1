param([string]$Destination=(Join-Path $PSScriptRoot '../out/deps/webview2'))
$ErrorActionPreference='Stop'
$version='1.0.3537.50'
$expected='5EA526BBD728ADDA0DA4D31219267E96460494A427E4894C4E09D9F320F4B9AA'
$Destination=[IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination|Out-Null
$package=Join-Path $Destination 'sdk.zip'
if(!(Test-Path $package) -or (Get-FileHash $package -Algorithm SHA256).Hash-ne$expected){
 Invoke-WebRequest -Uri "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$version/microsoft.web.webview2.$version.nupkg" -OutFile $package
}
if((Get-FileHash $package -Algorithm SHA256).Hash-ne$expected){throw 'WebView2 SDK checksum mismatch'}
Expand-Archive -LiteralPath $package -DestinationPath (Join-Path $Destination 'sdk') -Force
Write-Output '已准备固定版本 WebView2 SDK。网页电视还需要本机已安装 Microsoft Edge WebView2 Runtime。'
