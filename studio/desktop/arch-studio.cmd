@echo off
setlocal DisableDelayedExpansion
set "runtime=%~dp0..\node_modules\electron\dist\electron.exe"
if not exist "%runtime%" (
  echo Native Electron runtime missing. Build arch-studio-runtime first. 1>&2
  exit /b 1
)
if not exist "%~dp0..\dist\index.html" (
  echo Production assets missing. Run the Studio production build first. 1>&2
  exit /b 1
)
"%runtime%" "%~dp0main.cjs" %*
