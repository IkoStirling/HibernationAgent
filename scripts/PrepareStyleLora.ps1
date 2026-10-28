param(
 [string]$Python='C:/Users/lenovo/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe',
 [string]$EnvironmentRoot=(Join-Path $PSScriptRoot '../out/deps/style-lora-venv'),
 [ValidateSet('cpu','cu128')][string]$TorchBuild='cpu',
 [string]$DownloadModel='',
 [string]$ModelDirectory=(Join-Path $PSScriptRoot '../out/models/style-base')
)
$ErrorActionPreference='Stop'
$EnvironmentRoot=[IO.Path]::GetFullPath($EnvironmentRoot)
New-Item -ItemType Directory -Path (Join-Path $EnvironmentRoot 'tmp') -Force|Out-Null
$env:TEMP=Join-Path $EnvironmentRoot 'tmp';$env:TMP=$env:TEMP
$env:PIP_CACHE_DIR=Join-Path $EnvironmentRoot 'pip-cache'
& $Python -m venv $EnvironmentRoot
if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
$venvPython=Join-Path $EnvironmentRoot 'Scripts/python.exe'
$matching=& $venvPython -c 'import importlib.util; s=importlib.util.find_spec("torch"); import sys; sys.exit(2) if s is None else None; import torch; print("cpu" if torch.version.cuda is None else "cu"+torch.version.cuda.replace(".",""))' 2>$null
if($LASTEXITCODE-ne0-or$matching-ne$TorchBuild){
 & $venvPython -m pip install --disable-pip-version-check --force-reinstall 'torch>=2.6,<3' --index-url ('https://download.pytorch.org/whl/'+$TorchBuild)
 if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
}
& $venvPython -m pip install --disable-pip-version-check -r (Join-Path $PSScriptRoot '../plugins/style-lora/requirements.txt')
if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
& $venvPython -c 'import torch,transformers,peft,safetensors;print("Style LoRA ready; CUDA available:",torch.cuda.is_available())'
if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
Write-Output "在插件设置填写此训练 Python：$venvPython"
if($DownloadModel){
 if($DownloadModel-notmatch'^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'){throw 'Model must be an explicit Hugging Face owner/repository identifier'}
 $ModelDirectory=[IO.Path]::GetFullPath($ModelDirectory)
 $download='import sys;from huggingface_hub import snapshot_download;snapshot_download(repo_id=sys.argv[1],local_dir=sys.argv[2],allow_patterns=["*.json","*.safetensors","*.model","*.txt","*.tiktoken"]);print("Local model:",sys.argv[2])'
 & $venvPython -c $download $DownloadModel $ModelDirectory
 if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
}
