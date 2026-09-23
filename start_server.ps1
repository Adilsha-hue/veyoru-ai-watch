$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot

function Read-SecretText([string]$Prompt) {
    $secure = Read-Host $Prompt -AsSecureString
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try {
        [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
    }
}

function Get-ProviderError($ErrorRecord) {
    $response = $ErrorRecord.Exception.Response
    if ($null -ne $response) {
        try {
            $stream = $response.GetResponseStream()
            $reader = [IO.StreamReader]::new($stream)
            return $reader.ReadToEnd()
        }
        catch {}
    }
    return $ErrorRecord.Exception.Message
}

if (-not $env:GROQ_API_KEY) {
    $env:GROQ_API_KEY = Read-SecretText "Paste your Groq API key (input is hidden)"
}
$env:GROQ_API_KEY = $env:GROQ_API_KEY.Trim()
$env:LLM_PROVIDER = "groq"

if (-not $env:GROQ_API_KEY.StartsWith("gsk_")) {
    throw "That does not look like a Groq API key. Create a current key at https://console.groq.com/keys"
}

Write-Host "Validating Groq credential..."
$headers = @{ Authorization = "Bearer $($env:GROQ_API_KEY)" }
try {
    $models = Invoke-RestMethod -Uri "https://api.groq.com/openai/v1/models" -Headers $headers -TimeoutSec 25
}
catch {
    $detail = Get-ProviderError $_
    throw "Groq rejected the credential or connection. Provider response: $detail"
}

$available = @($models.data | ForEach-Object { $_.id })
$candidates = @(
    "llama-3.1-8b-instant",
    "llama-3.3-70b-versatile"
) + @($available | Where-Object {
    $_ -notmatch "whisper|guard|orpheus|tts|embedding"
})
$candidates = @($candidates | Where-Object { $available -contains $_ } | Select-Object -Unique -First 8)

$chosen = $null
$lastProviderError = "No compatible chat model was returned."
foreach ($candidate in $candidates) {
    $probeBody = @{
        model = $candidate
        messages = @(@{ role = "user"; content = "Reply only: VEYORU online" })
        max_completion_tokens = 256
        temperature = 0
    }
    if ($candidate.StartsWith("openai/gpt-oss-")) {
        $probeBody.reasoning_effort = "low"
        $probeBody.include_reasoning = $false
    }
    $probeBody = $probeBody | ConvertTo-Json -Depth 5
    try {
        $probe = Invoke-RestMethod -Uri "https://api.groq.com/openai/v1/chat/completions" `
            -Method Post -Headers $headers -ContentType "application/json" `
            -Body $probeBody -TimeoutSec 25
        $probeAnswer = [string]$probe.choices[0].message.content
        if ($probeAnswer.Trim()) {
            $chosen = $candidate
            break
        }
        $lastProviderError = "Model $candidate returned no final answer."
    }
    catch {
        $lastProviderError = Get-ProviderError $_
    }
}
if (-not $chosen) {
    throw "Groq accepted the key but rejected every available chat model. Provider response: $lastProviderError"
}
$env:GROQ_MODEL = $chosen
Write-Host "Groq accepted the credential and completion test. Model: $chosen"

$listeners = @(Get-NetTCPConnection -LocalPort 8000 -State Listen -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty OwningProcess -Unique)
foreach ($listenerPid in $listeners) {
    Write-Host "Stopping stale server PID $listenerPid..."
    Stop-Process -Id $listenerPid -Force
}

$python = Join-Path $PSScriptRoot ".venv-platformio\Scripts\python.exe"
if (-not (Test-Path -LiteralPath $python)) {
    $python = (Get-Command python -ErrorAction Stop).Source
}

$stdout = Join-Path $PSScriptRoot "server.stdout.log"
$stderr = Join-Path $PSScriptRoot "server.stderr.log"
$process = Start-Process -FilePath $python `
    -ArgumentList @("server.py", "--host", "0.0.0.0", "--port", "8000") `
    -WorkingDirectory $PSScriptRoot `
    -WindowStyle Hidden `
    -RedirectStandardOutput $stdout `
    -RedirectStandardError $stderr `
    -PassThru

Start-Sleep -Seconds 2
if ($process.HasExited) {
    throw "VEYORU server exited during startup. See $stderr"
}

$body = @{ text = "Reply only: VEYORU online" } | ConvertTo-Json
try {
    $result = Invoke-RestMethod -Uri "http://127.0.0.1:8000/api/assistant" `
        -Method Post -ContentType "application/json" -Body $body -TimeoutSec 35
    $health = Invoke-RestMethod -Uri "http://127.0.0.1:8000/api/health" -TimeoutSec 10
}
catch {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    throw "VEYORU local test failed: $($_.Exception.Message)"
}

if ($result.mode -ne "groq") {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    throw "VEYORU started, but its AI test did not use Groq: $($result | ConvertTo-Json -Compress)"
}

Write-Host ""
Write-Host "VEYORU ONLINE" -ForegroundColor Green
Write-Host "Server PID: $($process.Id)"
Write-Host "Browser: http://localhost:8000"
foreach ($boardUrl in @($health.board_assistant_urls)) {
    Write-Host "Board API: $boardUrl"
}
Write-Host "Test answer: $($result.answer)"
