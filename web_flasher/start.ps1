$ErrorActionPreference = "Stop"

Set-Location $PSScriptRoot

$pythonCmd = $null
foreach ($candidate in @("py", "python")) {
    $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
    if ($cmd) {
        $pythonCmd = $candidate
        break
    }
}

if (-not $pythonCmd) {
    $commonPaths = @(
        "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe",
        "$env:LOCALAPPDATA\Programs\Python\Python311\python.exe",
        "$env:LOCALAPPDATA\Programs\Python\Python310\python.exe",
        "$env:ProgramFiles\Python312\python.exe",
        "$env:ProgramFiles\Python311\python.exe",
        "$env:ProgramFiles\Python310\python.exe"
    )

    foreach ($path in $commonPaths) {
        if (Test-Path $path) {
            $pythonCmd = $path
            break
        }
    }
}

if (-not $pythonCmd) {
    throw "Python 3.10+ was not found on this Windows machine. Install Python and reopen PowerShell before running this flasher."
}

$venvPath = Join-Path $PSScriptRoot ".venv"
if (-not (Test-Path $venvPath)) {
    Write-Host "Creating virtual environment..."
    & $pythonCmd -m venv $venvPath
}

$pythonExe = Join-Path $venvPath "Scripts\python.exe"
& $pythonExe -m pip install --upgrade pip
& $pythonExe -m pip install -r requirements.txt

Write-Host "Starting local web flasher at http://127.0.0.1:8000"
& $pythonExe app.py --host 127.0.0.1 --port 8000
