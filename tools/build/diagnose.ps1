$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$tools = @{}
foreach ($name in @('git', 'cmake', 'clang++', 'arm-none-eabi-g++', 'arm-none-eabi-readelf', 'arm-none-eabi-nm', 'python', 'pdftotext')) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    $tools[$name] = if ($command) { $command.Source } else { $null }
}
$result = [ordered]@{
    date_utc = [DateTime]::UtcNow.ToString('o')
    os = [Environment]::OSVersion.VersionString
    powershell = $PSVersionTable.PSVersion.ToString()
    workspace = $root
    tools = $tools
}
New-Item -ItemType Directory -Path (Join-Path $root 'build') -Force | Out-Null
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'build/diagnostic.json') -Encoding utf8
$result | ConvertTo-Json -Depth 5
if (-not $tools['cmake'] -or -not $tools['python']) { throw 'CMake et Python requis pour les commandes de développement.' }
