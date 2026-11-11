[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (!$vs) { throw 'Visual Studio C++ tools not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $OutputDirectory).Path
$source = Join-Path $PSScriptRoot 'private_data_test.cpp'
Push-Location $outputRoot
try {
  & cmd.exe /d /c "call `"$vcvars`" x86 >nul && cl /nologo /std:c++17 /EHsc /W4 /WX /MDd /Zi /Od /Fe:private_data_test.exe `"$source`" && private_data_test.exe"
  if ($LASTEXITCODE -ne 0) { throw "Private data test failed: $LASTEXITCODE" }
} finally {
  Pop-Location
}
