@echo off
"%~dp0..\runtime\windows-x64\git\cmd\git.exe" %*
exit /b %errorlevel%
