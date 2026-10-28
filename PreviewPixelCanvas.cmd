@echo off
pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\StartPixelCanvasPreview.ps1" %*
if errorlevel 1 pause
