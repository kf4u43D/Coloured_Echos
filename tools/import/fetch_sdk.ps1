$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$destination = Join-Path $root 'third_party/distingNT_API_1.18.0'
$commit = 'f440d20c2a96a9ca43892c7418a1d8185ad47eb5'
$gitOptions = @('-c', 'http.sslBackend=openssl', '-c', ('safe.directory=' + $destination.Replace('\', '/')))
function Test-PinnedSnapshot {
    $manifest = Get-Content -LiteralPath (Join-Path $root 'third_party/sdk_1.18_source.json') -Raw | ConvertFrom-Json
    foreach ($file in $manifest.files) {
        $path = Join-Path $destination $file.path
        if (!(Test-Path -LiteralPath $path)) { throw "Fichier SDK absent : $path" }
        $bytes = [Text.Encoding]::UTF8.GetBytes([IO.File]::ReadAllText($path).Replace("`r`n", "`n"))
        $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
        if ($hash -ne $file.canonical_text_sha256) { throw "Snapshot SDK différent, préservé : $path" }
    }
}
if (Test-Path -LiteralPath $destination) {
    Test-PinnedSnapshot
    Write-Output "SDK 1.18.0 déjà vérifié : $commit (API13)"
    exit 0
}
New-Item -ItemType Directory -Path (Join-Path $root 'third_party') -Force | Out-Null
& git @gitOptions clone --no-checkout 'https://github.com/expertsleepersltd/distingNT_API.git' $destination
if ($LASTEXITCODE -ne 0) { throw 'Récupération du SDK échouée.' }
& git @gitOptions -C $destination checkout --detach $commit
if ($LASTEXITCODE -ne 0) { throw 'Épinglage du SDK échoué.' }
Test-PinnedSnapshot
Write-Output "SDK acquis : $commit (1.18.0/API13, sans sous-module optionnel)"
