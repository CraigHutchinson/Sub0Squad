@echo off
rem git, except that `describe` only considers semantic-version tags (v1.2.3): the Windows twin of git-semver.sh (cmake/dependencies.cmake,
rem docs/INTERVENTIONS.md row 37). The arguments are scanned with shift, not `for`, which would expand `v[0-9]*` as a file pattern.
setlocal
set "s0qArgs=%*"
:scan
if "%~1"=="" goto plain
if /i "%~1"=="describe" goto describe
shift
goto scan
:describe
git %s0qArgs% --match "v[0-9]*.[0-9]*"
exit /b %errorlevel%
:plain
git %s0qArgs%
exit /b %errorlevel%
