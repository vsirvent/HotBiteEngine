<#
.SYNOPSIS
    One-time setup of the Meshy AI API key used by the hotbite-editor MCP server's
    meshy_* tools (Tools/SceneEditor/mcp/src/meshy-tools.js).

.DESCRIPTION
    Writes ONLY %TEMP%\HotBiteMeshy\config.json (or HOTBITE_MESHY_DIR\config.json,
    if set) - never a file under the project or the engine repo, so the key can
    never end up in a commit or an uploaded project by mistake (see CLAUDE.md's
    note on secrets).

    Run this yourself, directly in PowerShell, or use the Scene Editor's own
    "Claude/Set Meshy API Key..." menu item (MeshySetup.h), which runs this same
    script in the background with stdin piped from its password field - the key
    still never goes near the agent or a chat transcript, only this process. There
    is deliberately no MCP tool or automation command that accepts the key:
    anything an agent tool takes as an argument becomes part of that
    conversation's transcript, which is exactly where a secret must never go.

.PARAMETER MaxCreditsPerTask
    Optional hard cap: meshy_* tools refuse to run (even with a valid confirm
    token) any single generation quoted above this many credits.

.PARAMETER MaxCreditsPerDay
    Optional hard cap: refuses a generation that would bring today's total
    consumed credits (per the local ledger) over this number.
#>
param(
	[int]$MaxCreditsPerTask = 0,
	[int]$MaxCreditsPerDay = 0
)

$ErrorActionPreference = 'Stop'

$dir = $env:HOTBITE_MESHY_DIR
if ([string]::IsNullOrWhiteSpace($dir)) {
	$dir = Join-Path ([System.IO.Path]::GetTempPath()) 'HotBiteMeshy'
}
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$configPath = Join-Path $dir 'config.json'

Write-Host "This writes only $configPath - never anything under the project or engine repo."

# Masked, character-by-character typing (Read-Host -AsSecureString) needs a real
# console to intercept keystrokes from - it throws when stdin is redirected, which
# is exactly how the Scene Editor's own menu item drives this (a pipe from its
# password field, no console at all). There is nothing left to mask in that case
# anyway: nothing is echoing to a visible terminal for a piped, hidden process, so
# a plain line read is both the only option and not a step down in secrecy.
if ([Console]::IsInputRedirected) {
	$apiKey = [Console]::In.ReadLine()
}
else {
	$secure = Read-Host -Prompt 'Meshy API key (from meshy.ai account settings)' -AsSecureString
	$bstr = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
	try {
		$apiKey = [System.Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
	}
	finally {
		[System.Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
	}
}
if ([string]::IsNullOrWhiteSpace($apiKey)) {
	Write-Error 'No key entered; nothing written.'
	exit 1
}

$config = [ordered]@{ api_key = $apiKey }
if ($MaxCreditsPerTask -gt 0) { $config.max_credits_per_task = $MaxCreditsPerTask }
if ($MaxCreditsPerDay -gt 0) { $config.max_credits_per_day = $MaxCreditsPerDay }

$json = $config | ConvertTo-Json
# ASCII-safe, no BOM - CLAUDE.md's own BOM trap applies to any file the editor or
# an included tool later reads, and there is no reason to risk it here either.
[System.IO.File]::WriteAllText($configPath, $json, [System.Text.UTF8Encoding]::new($false))

Write-Host "Wrote $configPath."
Write-Host 'Ask the agent to run meshy_status to confirm it can see the key and check your balance.'
