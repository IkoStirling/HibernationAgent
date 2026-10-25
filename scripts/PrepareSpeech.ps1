param([string]$RuntimeRoot='E:/Aliyat/VoiceRuntime',[Parameter(Mandatory)][string]$PythonExecutable,[string]$BuildRoot=(Join-Path $PSScriptRoot '../out/build'),[switch]$SkipInstall)
$ErrorActionPreference='Stop'
$RuntimeRoot=[IO.Path]::GetFullPath($RuntimeRoot);$PythonExecutable=[IO.Path]::GetFullPath($PythonExecutable)
if(!(Test-Path -LiteralPath $PythonExecutable -PathType Leaf)){throw 'Python runtime is unavailable.'}
$modules=Join-Path $RuntimeRoot 'python';$env:TEMP=Join-Path $RuntimeRoot 'tmp';$env:TMP=$env:TEMP;$env:PIP_CACHE_DIR=Join-Path $RuntimeRoot 'cache'
New-Item -ItemType Directory -Path $modules,$env:TEMP -Force|Out-Null
if(!$SkipInstall){
 & $PythonExecutable -m pip install --target $modules --only-binary=:all: 'pyopenjtalk-plus==0.4.1.post10' 'pypinyin==0.55.0'
 if($LASTEXITCODE-ne0){throw 'Speech dependencies could not be prepared.'}
}
$destination=Join-Path ([IO.Path]::GetFullPath($BuildRoot)) 'plugins/tts';New-Item -ItemType Directory -Path $destination -Force|Out-Null
foreach($file in @('kana_worker.py','kana_reading.py')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot "../plugins/tts/$file") -Destination (Join-Path $destination $file)}
$runtime=@{pythonExecutable=$PythonExecutable;modules=$modules}|ConvertTo-Json
$runtime|Set-Content -LiteralPath (Join-Path $destination 'runtime.json') -Encoding utf8
$env:PYTHONDONTWRITEBYTECODE='1'; & $PythonExecutable -B (Join-Path $destination 'kana_worker.py') --check
if($LASTEXITCODE-ne0){throw 'Speech runtime check failed; Mei remains unavailable.'}
$voiceLicense=Join-Path $modules 'pyopenjtalk/htsvoice/LICENSE_mei_normal.htsvoice'
if(Test-Path -LiteralPath $voiceLicense){Copy-Item -LiteralPath $voiceLicense -Destination (Join-Path $destination 'LICENSE-Mei.txt')}
Write-Output '本地合成音已就绪：Mei · 中文假名（普通版）。在声音设置中选择，语速倍率 1 对应普通试听版。'
