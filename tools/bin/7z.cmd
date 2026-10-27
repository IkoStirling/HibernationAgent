@echo off
"%~dp0..\runtime\windows-x64\7zip\7z.exe" %*
exit /b %errorlevel%
