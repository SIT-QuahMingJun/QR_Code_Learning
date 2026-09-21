[CmdletBinding()]
param(
    [int]$Port = 8080,
    [string]$EmsdkPath = 'C:\Users\quahm\Downloads\Learning\School (Sem 3)\CSD 2101 Computer Graphics\Sandbox Android\Wasm\emsdk'
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSCommandPath
$emsdkEnv = Join-Path $EmsdkPath 'emsdk_env.bat'
$wasmBuild = Join-Path $projectRoot 'build\wasm'
$url = "http://localhost:$Port/index.html"

if (-not (Test-Path -LiteralPath $emsdkEnv -PathType Leaf)) {
    throw "Emscripten setup script was not found: $emsdkEnv`nPass its folder with: .\run-wasm.ps1 -EmsdkPath 'C:\path\to\emsdk'"
}

# Kill stale servers from previous runs. A leftover "python -m http.server"
# keeps build\wasm files open, which makes the next build fail with
# PermissionError. Free the port and kill matching servers first.
Write-Host "Checking for stale servers on port $Port..."
$staleConnections = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
foreach ($connection in $staleConnections) {
    $owner = Get-Process -Id $connection.OwningProcess -ErrorAction SilentlyContinue
    if ($owner) {
        Write-Warning "Stopping '$($owner.ProcessName)' (PID $($owner.Id)) which is listening on port $Port."
        Stop-Process -Id $owner.Id -Force -ErrorAction SilentlyContinue
    }
}

# Also catch servers on other ports that still serve this build folder.
$staleServers = Get-CimInstance Win32_Process -Filter "Name='python.exe' OR Name='py.exe'" |
    Where-Object { $_.CommandLine -match 'http\.server' -and $_.CommandLine -like "*$wasmBuild*" }
foreach ($server in $staleServers) {
    Write-Warning "Stopping stale http.server (PID $($server.ProcessId)) serving this build folder."
    Stop-Process -Id $server.ProcessId -Force -ErrorAction SilentlyContinue
}
Start-Sleep -Milliseconds 300

# A .bat file cannot alter its parent PowerShell process. Run it in cmd, read
# the resulting environment, then copy those variables into this process.
$environmentLines = & $env:ComSpec /d /s /c "call `"$emsdkEnv`" >nul 2>nul && set"
if ($LASTEXITCODE -ne 0) {
    throw 'Emscripten environment setup failed.'
}
$importedNames = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($line in $environmentLines) {
    $pair = $line -split '=', 2
    if ($pair.Count -eq 2 -and $pair[0] -and $importedNames.Add($pair[0])) {
        # Set the PowerShell environment provider directly. This updates the
        # current session's PATH as well as the environment inherited by tools.
        # cmd can emit both PATH and Path; retain the first (Emscripten's) one.
        Set-Item -Path "Env:$($pair[0])" -Value $pair[1]
    }
}

if (-not (Get-Command emcmake -ErrorAction SilentlyContinue)) {
    throw 'Emscripten was initialised, but emcmake is still unavailable.'
}

Write-Host 'Configuring WASM demo...'
& emcmake cmake -S $projectRoot -B $wasmBuild -DCMAKE_BUILD_TYPE=Debug
if ($LASTEXITCODE -ne 0) { throw 'WASM configuration failed.' }

Write-Host 'Building WASM demo...'
$buildSucceeded = $false
for ($attempt = 1; $attempt -le 3; $attempt++) {
    & cmake --build $wasmBuild --config Debug
    if ($LASTEXITCODE -eq 0) {
        $buildSucceeded = $true
        break
    }
    if ($attempt -lt 3) {
        Write-Warning "The generated browser files are temporarily in use. Retrying build ($attempt/3)..."
        Start-Sleep -Seconds 1
    }
}
if (-not $buildSucceeded) {
    throw 'WASM build failed. Close any browser tab or local server using this project, then run the launcher again.'
}

$python = Get-Command python -ErrorAction SilentlyContinue
$pythonArguments = @()
if (-not $python) {
    $python = Get-Command py -ErrorAction SilentlyContinue
    $pythonArguments = @('-3')
}
if (-not $python) {
    throw 'Python 3 was not found. Install Python 3 to run the local server.'
}

$serverJob = Start-Job -ScriptBlock {
    param($PythonPath, $PythonArgs, $ServerPort, $Directory)
    & $PythonPath @PythonArgs -m http.server $ServerPort --directory $Directory
} -ArgumentList $python.Source, $pythonArguments, $Port, $wasmBuild

try {
    Start-Sleep -Milliseconds 800
    if ($serverJob.State -eq 'Failed') {
        Receive-Job $serverJob -Keep
        throw 'The local web server could not start.'
    }

    $chromePath = @(
        (Join-Path ${env:ProgramFiles} 'Google\Chrome\Application\chrome.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Google\Chrome\Application\chrome.exe')
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1

    if ($chromePath) {
        Start-Process -FilePath $chromePath -ArgumentList $url
    } else {
        Start-Process $url
    }

    Write-Host "Serving the fresh WASM build at $url"
    Write-Host 'Keep this window open. Press Ctrl+C when you want to stop the local server.'
    Wait-Job $serverJob | Out-Null
} finally {
    if ($serverJob) {
        Stop-Job $serverJob -ErrorAction SilentlyContinue
        Remove-Job $serverJob -Force -ErrorAction SilentlyContinue
    }
}
