<#
.SYNOPSIS
  Windows PowerShell client for Uart_Esp32 v0.33 and newer (client fix v0.34).

.DESCRIPTION
  Supports a one-time pairing handshake, an interactive UART terminal and
  authenticated file upload/download. PowerShell access must first be enabled
  in the ESP32 web UI under UART Einstellungen.

.EXAMPLES
  .\tools\UartEsp32.ps1 -Connect 192.168.4.1
  .\tools\UartEsp32.ps1 -Esp http://192.168.4.1 -Action console
  .\tools\UartEsp32.ps1 -Esp http://192.168.4.1 -Action upload -LocalPath .\ptybridge -TargetPath /tmp/bx3test/ptybridge
  .\tools\UartEsp32.ps1 -Esp http://192.168.4.1 -Action download -Source /tmp/mtd7.img -OutFile .\mtd7.img
#>
param(
  [string]$Connect = '',
  [string]$Esp = $env:UART_ESP32,
  [ValidateSet('console','status','console-read','console-send','console-control','heartbeat','upload','upload-status','abort-upload','download','download-status','abort-download','uart-start','uart-stop')]
  [string]$Action = 'status',
  [string]$Text = '',
  [ValidateSet('none','CR','LF','CRLF')]
  [string]$LineEnding = 'CR',
  [ValidateSet('ctrl-c','ctrl-d','tab','esc')]
  [string]$Control = 'ctrl-c',
  [string]$LocalPath = '',
  [string]$TargetPath = '/tmp/bx3test/ptybridge',
  [string]$Source = '/tmp/mtd7.img',
  [string]$OutFile = '',
  [int]$StartBlock = 0,
  [switch]$NoExec,
  [switch]$Enable,
  [switch]$Disable,
  [int]$Since = 0,
  [int]$PollMs = 150,
  [int]$TimeoutSeconds = 60
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if (![string]::IsNullOrWhiteSpace($Connect)) {
  $Esp = $Connect
  $Action = 'console'
}
if ([string]::IsNullOrWhiteSpace($Esp)) {
  throw 'ESP-Adresse fehlt. Beispiel: .\tools\UartEsp32.ps1 -Connect 192.168.4.1'
}
if ($Esp -notmatch '^https?://') { $Esp = 'http://' + $Esp }
$script:BaseUrl = $Esp.TrimEnd('/')
$script:Token = ''
$script:DeviceId = ''

function Join-UartUri([string]$Path) {
  if ($Path.StartsWith('/')) { return $script:BaseUrl + $Path }
  return $script:BaseUrl + '/' + $Path
}

function ConvertTo-FormBody([hashtable]$Body) {
  $pairs = New-Object System.Collections.Generic.List[string]
  foreach ($key in $Body.Keys) {
    $name = [Uri]::EscapeDataString([string]$key)
    $value = [Uri]::EscapeDataString([string]$Body[$key])
    $pairs.Add($name + '=' + $value)
  }
  return ($pairs -join '&')
}

function Get-PowerShellHeaders {
  $headers = @{ 'Cache-Control' = 'no-store'; 'X-UartEsp32-Client' = 'powershell' }
  if (![string]::IsNullOrWhiteSpace($script:Token)) { $headers['X-UartEsp32-Token'] = $script:Token }
  return $headers
}

function Invoke-UartGet([string]$Path) {
  Invoke-RestMethod -Uri (Join-UartUri $Path) -Method Get -Headers (Get-PowerShellHeaders)
}

function Invoke-UartForm([string]$Path, [hashtable]$Body = @{}) {
  Invoke-RestMethod -Uri (Join-UartUri $Path) -Method Post -Headers (Get-PowerShellHeaders) -ContentType 'application/x-www-form-urlencoded; charset=utf-8' -Body (ConvertTo-FormBody $Body)
}

function Get-PairingDirectory {
  $root = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { Join-Path $HOME '.uart-esp32' }
  return (Join-Path $root 'UartEsp32')
}

function Get-PairingFile([string]$DeviceId) {
  $safe = $DeviceId -replace '[^A-Za-z0-9._-]', '_'
  return (Join-Path (Get-PairingDirectory) ($safe + '.json'))
}

function Save-Pairing([string]$DeviceId, [string]$Token) {
  $dir = Get-PairingDirectory
  if (!(Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
  $record = [ordered]@{ device_id = $DeviceId; base_url = $script:BaseUrl; token = $Token; paired_at = (Get-Date).ToString('o') }
  $record | ConvertTo-Json | Set-Content -LiteralPath (Get-PairingFile $DeviceId) -Encoding UTF8
}

function Load-Pairing([string]$DeviceId) {
  $file = Get-PairingFile $DeviceId
  if (!(Test-Path -LiteralPath $file -PathType Leaf)) { return $null }
  try { return (Get-Content -LiteralPath $file -Raw | ConvertFrom-Json) } catch { return $null }
}

function Ensure-PowerShellSession {
  $status = Invoke-RestMethod -Uri (Join-UartUri '/uart/powershell/status') -Method Get -Headers @{ 'Cache-Control' = 'no-store' }
  $script:DeviceId = [string]$status.device_id
  if (-not [bool]$status.enabled) {
    throw 'PowerShell-Steuerung ist deaktiviert. In der Weboberflaeche: UART Einstellungen -> PowerShell-Steuerung aktivieren.'
  }

  $saved = Load-Pairing $script:DeviceId
  if ([bool]$status.paired) {
    if ($null -eq $saved -or [string]::IsNullOrWhiteSpace([string]$saved.token)) {
      throw 'ESP32 ist bereits gekoppelt, aber auf diesem PC fehlt das Token. In UART Einstellungen die PowerShell-Kopplung zuruecksetzen und danach erneut verbinden.'
    }
    $script:Token = [string]$saved.token
    try { Invoke-UartGet '/uart/powershell/auth' | Out-Null }
    catch { throw 'Gespeicherte PowerShell-Kopplung ist nicht mehr gueltig. Kopplung in UART Einstellungen zuruecksetzen und erneut verbinden.' }
    return $status
  }

  Write-Host 'Erste Verbindung erkannt - einmaliger Handshake wird ausgefuehrt ...'
  $pair = Invoke-RestMethod -Uri (Join-UartUri '/uart/powershell/pair') -Method Post -ContentType 'application/x-www-form-urlencoded' -Body ''
  $script:Token = [string]$pair.token
  if ([string]::IsNullOrWhiteSpace($script:Token)) { throw 'Handshake lieferte kein Kopplungstoken.' }
  Save-Pairing ([string]$pair.device_id) $script:Token
  Invoke-UartGet '/uart/powershell/auth' | Out-Null
  Write-Host ('Handshake erfolgreich. Kopplung lokal gespeichert: {0}' -f (Get-PairingFile ([string]$pair.device_id)))
  return $status
}

function Get-UartUploadStatus { Invoke-UartGet '/uart/upload/status' }
function Get-UartDownloadStatus { Invoke-UartGet '/uart/image/status' }

function Wait-UartUpload([scriptblock]$Predicate, [int]$Seconds = 30) {
  $deadline = (Get-Date).AddSeconds($Seconds)
  do {
    $s = Get-UartUploadStatus
    if ($s.error) { throw ([string]$s.status) }
    if (& $Predicate $s) { return $s }
    Start-Sleep -Milliseconds $PollMs
  } while ((Get-Date) -lt $deadline)
  throw 'Timeout beim Warten auf Upload-Status.'
}

function Wait-UartDownload([scriptblock]$Predicate, [int]$Seconds = 30) {
  $deadline = (Get-Date).AddSeconds($Seconds)
  do {
    $s = Get-UartDownloadStatus
    if ($s.error) { throw ([string]$s.status) }
    if (& $Predicate $s) { return $s }
    Start-Sleep -Milliseconds $PollMs
  } while ((Get-Date) -lt $deadline)
  throw 'Timeout beim Warten auf Download-Status.'
}

function Get-LineEndingBytes([string]$Mode) {
  switch ($Mode) {
    'CR'   { return "`r" }
    'LF'   { return "`n" }
    'CRLF' { return "`r`n" }
    default { return '' }
  }
}

function Show-Object($Object) { $Object | ConvertTo-Json -Depth 8 }

function Get-OptionalProperty($Object, [string]$Name, $Default = $null) {
  if ($null -eq $Object) { return $Default }
  $prop = $Object.PSObject.Properties[$Name]
  if ($null -eq $prop) { return $Default }
  return $prop.Value
}

function Send-UartUploadChunk([byte[]]$Chunk, [Int64]$Offset) {
  $end = $Offset + $Chunk.Length
  $b64 = [Convert]::ToBase64String($Chunk)
  for ($attempt = 1; $attempt -le 6; $attempt++) {
    $s = Get-UartUploadStatus
    if ([Int64]$s.accepted_bytes -eq $end -and $s.phase -eq 'ready') { return }
    $needPost = $true
    if ([Int64]$s.accepted_bytes -eq $Offset -and $s.phase -eq 'chunk_verify') { $needPost = $false }
    if ($needPost) {
      try { Invoke-UartForm '/uart/upload/chunk' @{ offset = $Offset; data = $b64 } | Out-Null }
      catch { if ($attempt -ge 6) { throw }; Start-Sleep -Milliseconds 750 }
    }
    try {
      Wait-UartUpload { param($x) (([Int64]$x.accepted_bytes -eq $end -and $x.phase -eq 'ready') -or ($x.retry_requested -and [Int64]$x.accepted_bytes -eq $Offset -and $x.phase -eq 'ready')) } 20 | Out-Null
      $s = Get-UartUploadStatus
      if ([Int64]$s.accepted_bytes -eq $end) { return }
    } catch { if ($attempt -ge 6) { throw } }
    Write-Host ("Block ab Offset {0} wird wiederholt ({1}/6)." -f $Offset, $attempt)
    Start-Sleep -Milliseconds 750
  }
  throw ("Uploadblock ab Offset {0} konnte nicht bestaetigt werden." -f $Offset)
}

function Start-UartUpload([string]$FileName, [string]$RemoteTarget, [bool]$Executable) {
  if ([string]::IsNullOrWhiteSpace($FileName)) { $FileName = Read-Host 'Lokale Datei' }
  if ([string]::IsNullOrWhiteSpace($RemoteTarget)) { $RemoteTarget = Read-Host 'BX3-Zielpfad' }
  if (!(Test-Path -LiteralPath $FileName -PathType Leaf)) { throw "Lokale Datei nicht gefunden: $FileName" }
  $item = Get-Item -LiteralPath $FileName
  $size = [Int64]$item.Length
  if ($size -le 0) { throw 'Leere Dateien werden von der Firmware aktuell abgewiesen.' }

  Write-Host ('Upload: {0} -> {1} ({2} Byte)' -f $item.FullName, $RemoteTarget, $size)
  $execText = if ($Executable) { '1' } else { '0' }
  Invoke-UartForm '/uart/upload/start' @{ target = $RemoteTarget; size = $size; exec = $execText } | Out-Null
  Wait-UartUpload { param($x) $x.phase -eq 'ready' } $TimeoutSeconds | Out-Null

  $fs = [System.IO.File]::OpenRead($item.FullName)
  try {
    $buffer = New-Object byte[] 4096
    $offset = [Int64]0
    while (($read = $fs.Read($buffer, 0, $buffer.Length)) -gt 0) {
      if ($read -eq $buffer.Length) { $chunk = $buffer }
      else { $chunk = New-Object byte[] $read; [Array]::Copy($buffer, 0, $chunk, 0, $read) }
      Send-UartUploadChunk $chunk $offset
      $offset += $read
      Write-Progress -Activity 'UART Datei-Upload' -Status ("{0} / {1} Byte" -f $offset, $size) -PercentComplete ([int](100 * $offset / $size))
    }
  } finally {
    $fs.Dispose()
    Write-Progress -Activity 'UART Datei-Upload' -Completed
  }

  Invoke-UartForm '/uart/upload/finish' @{} | Out-Null
  $done = Wait-UartUpload { param($x) $x.done } 60
  Write-Host 'Upload abgeschlossen.'
  return $done
}

function Get-DefaultDownloadName([string]$RemoteSource) {
  if ($RemoteSource -match '/dev/mtd(\d+)ro$') { return "bx3-mtd$($Matches[1]).img" }
  $name = Split-Path -Leaf $RemoteSource
  if ([string]::IsNullOrWhiteSpace($name)) { return 'bx3-download.bin' }
  return ($name -replace '[^A-Za-z0-9._-]', '_')
}

function Start-UartDownload([string]$RemoteSource, [string]$Destination, [int]$FirstBlock) {
  if ([string]::IsNullOrWhiteSpace($RemoteSource)) { $RemoteSource = Read-Host 'BX3-Quelldatei' }
  if ([string]::IsNullOrWhiteSpace($Destination)) {
    $suggested = Join-Path (Get-Location) (Get-DefaultDownloadName $RemoteSource)
    $entered = Read-Host ("Lokale Zieldatei [{0}]" -f $suggested)
    $Destination = if ([string]::IsNullOrWhiteSpace($entered)) { $suggested } else { $entered }
  }
  $fullDest = [System.IO.Path]::GetFullPath($Destination)
  $dir = [System.IO.Path]::GetDirectoryName($fullDest)
  if ($dir -and !(Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }

  Write-Host ('Download: {0} -> {1}' -f $RemoteSource, $fullDest)
  Invoke-UartForm '/uart/image/start' @{ source = $RemoteSource; start = $FirstBlock } | Out-Null
  $out = [System.IO.File]::Open($fullDest, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
  $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("uart-esp32-block-{0}.bin" -f ([Guid]::NewGuid()))
  try {
    while ($true) {
      $s = Wait-UartDownload { param($x) ($x.done -or $x.block_ready) } $TimeoutSeconds
      if ($s.done) { break }
      $block = [int]$s.ready_block
      Invoke-WebRequest -Uri (Join-UartUri ("/uart/image/block?block=$block")) -OutFile $tmp -Headers (Get-PowerShellHeaders) | Out-Null
      $bytes = [System.IO.File]::ReadAllBytes($tmp)
      if ($bytes.Length -le 0) { throw "Leerer Downloadblock $block." }
      $out.Write($bytes, 0, $bytes.Length)
      Invoke-UartForm '/uart/image/ack' @{ block = $block } | Out-Null
      $accepted = [Int64]$s.accepted_bytes + $bytes.Length
      if ([Int64]$s.total_size -gt 0) {
        Write-Progress -Activity 'UART Datei-Download' -Status ("Block {0}, {1} / {2} Byte" -f $block, $accepted, $s.total_size) -PercentComplete ([int]([Math]::Min(100, 100 * $accepted / [double]$s.total_size)))
      } else { Write-Progress -Activity 'UART Datei-Download' -Status ("Block {0}" -f $block) }
    }
  } finally {
    $out.Dispose()
    Write-Progress -Activity 'UART Datei-Download' -Completed
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force }
  }
  Write-Host ("Download abgeschlossen. Gespeichert: {0}" -f $fullDest)
  return (Get-UartDownloadStatus)
}

function Convert-HexToText([string]$Hex) {
  if ([string]::IsNullOrWhiteSpace($Hex)) { return '' }
  $clean = $Hex -replace '[^0-9A-Fa-f]', ''
  if ($clean.Length -lt 2) { return '' }
  $count = [int]($clean.Length / 2)
  $bytes = New-Object byte[] $count
  for ($i = 0; $i -lt $count; $i++) { $bytes[$i] = [Convert]::ToByte($clean.Substring($i * 2, 2), 16) }
  return [Text.Encoding]::UTF8.GetString($bytes)
}

function Split-LocalCommand([string]$Line) {
  $matches = [regex]::Matches($Line, '(?:"([^"]*)"|''([^'']*)''|(\S+))')
  $parts = New-Object System.Collections.Generic.List[string]
  foreach ($m in $matches) {
    if ($m.Groups[1].Success) { $parts.Add($m.Groups[1].Value) }
    elseif ($m.Groups[2].Success) { $parts.Add($m.Groups[2].Value) }
    else { $parts.Add($m.Groups[3].Value) }
  }
  return ,$parts.ToArray()
}

function Invoke-InteractiveLocalCommand([string]$Line) {
  if ($Line.StartsWith('//')) {
    Invoke-UartForm '/uart/console/send' @{ format = 'text'; data = ($Line.Substring(1) + "`r") } | Out-Null
    return $true
  }
  $parts = Split-LocalCommand $Line
  if ($parts.Count -eq 0) { return $true }
  $cmd = $parts[0].ToLowerInvariant()
  switch ($cmd) {
    '/help' {
      Write-Host "`nLokale Befehle:"
      Write-Host '  /upload ["C:\lokal\datei"] ["/tmp/ziel"]'
      Write-Host '  /download ["/tmp/quelle"] ["C:\lokal\datei"]'
      Write-Host '  /status'
      Write-Host '  /heartbeat on|off'
      Write-Host '  /clear'
      Write-Host '  /exit'
      Write-Host '  //pfad  sendet einen mit / beginnenden Befehl an BX3'
    }
    '/status' { Show-Object (Invoke-UartGet '/uart/status') }
    '/heartbeat' {
      $mode = if ($parts.Count -ge 2) { $parts[1].ToLowerInvariant() } else { '' }
      if ($mode -ne 'on' -and $mode -ne 'off') { Write-Host 'Verwendung: /heartbeat on|off'; break }
      Show-Object (Invoke-UartForm '/uart/console/heartbeat' @{ enabled = $(if ($mode -eq 'on') { '1' } else { '0' }) })
    }
    '/clear' { Clear-Host }
    '/upload' {
      $local = if ($parts.Count -ge 2) { $parts[1] } else { '' }
      $remote = if ($parts.Count -ge 3) { $parts[2] } else { '' }
      try {
        Start-UartUpload $local $remote $true | Out-Null
      } catch {
        Write-Host ("`n[Upload-Fehler: {0}]" -f $_.Exception.Message)
        Write-Host 'Die UART-Konsole bleibt verbunden. /upload erneut versuchen oder /help verwenden.'
      }
    }
    '/download' {
      $remote = if ($parts.Count -ge 2) { $parts[1] } else { '' }
      $local = if ($parts.Count -ge 3) { $parts[2] } else { '' }
      try {
        Start-UartDownload $remote $local 0 | Out-Null
      } catch {
        Write-Host ("`n[Download-Fehler: {0}]" -f $_.Exception.Message)
        Write-Host 'Die UART-Konsole bleibt verbunden. /download erneut versuchen oder /help verwenden.'
      }
    }
    '/exit' { return $false }
    default { Write-Host ("Unbekannter lokaler Befehl: {0}. /help zeigt die Befehle." -f $cmd) }
  }
  return $true
}

function Start-InteractiveConsole {
  Write-Host ('Uart_Esp32 PowerShell-Konsole - {0}' -f $script:DeviceId)
  Write-Host ('Verbunden mit {0}. /help fuer lokale Befehle, /exit zum Beenden.' -f $script:BaseUrl)
  Write-Host 'Normale Tasten werden direkt an UART gesendet. Ctrl+C/Ctrl+D/Tab/Esc werden weitergereicht.'

  $seq = 0
  $localMode = $false
  $localBuffer = New-Object Text.StringBuilder
  $oldCtrl = [Console]::TreatControlCAsInput
  [Console]::TreatControlCAsInput = $true
  try {
    while ($true) {
      try {
        $d = Invoke-UartGet ("/uart/console/data?since=$seq")
        $wasTruncated = [bool](Get-OptionalProperty $d 'truncated' $false)
        if ($wasTruncated) { Write-Host "`n[UART-Puffer wurde zwischenzeitlich gekuerzt]" }

        $hexData = [string](Get-OptionalProperty $d 'hex' '')
        $text = Convert-HexToText $hexData
        if ($text.Length -gt 0) { [Console]::Write($text) }

        $nextSequence = Get-OptionalProperty $d 'sequence' $seq
        if ($null -ne $nextSequence -and [string]$nextSequence -match '^\d+$') {
          $seq = [int64]$nextSequence
        }
      } catch {
        Write-Host ("`n[Polling-Fehler: {0}]" -f $_.Exception.Message)
        Start-Sleep -Milliseconds 500
      }

      while ([Console]::KeyAvailable) {
        $key = [Console]::ReadKey($true)
        $mods = $key.Modifiers
        if (($mods -band [ConsoleModifiers]::Control) -and $key.Key -eq [ConsoleKey]::C) {
          if ($localMode) {
            [Console]::Write("^C`r`n")
            $localMode = $false; $localBuffer.Clear() | Out-Null
          } else { Invoke-UartForm '/uart/console/control' @{ code = 3 } | Out-Null }
          continue
        }
        if (($mods -band [ConsoleModifiers]::Control) -and $key.Key -eq [ConsoleKey]::D) {
          if (!$localMode) { Invoke-UartForm '/uart/console/control' @{ code = 4 } | Out-Null }
          continue
        }
        if ($key.Key -eq [ConsoleKey]::Escape) {
          if ($localMode) { [Console]::Write("`r`n"); $localMode = $false; $localBuffer.Clear() | Out-Null }
          else { Invoke-UartForm '/uart/console/control' @{ code = 27 } | Out-Null }
          continue
        }
        if ($key.Key -eq [ConsoleKey]::Enter) {
          if ($localMode) {
            [Console]::Write("`r`n")
            $continue = Invoke-InteractiveLocalCommand $localBuffer.ToString()
            $localBuffer.Clear() | Out-Null; $localMode = $false
            if (-not $continue) { return }
          } else { Invoke-UartForm '/uart/console/send' @{ format = 'text'; data = "`r" } | Out-Null }
          continue
        }
        if ($key.Key -eq [ConsoleKey]::Backspace) {
          if ($localMode) {
            if ($localBuffer.Length -gt 0) { $localBuffer.Length--; [Console]::Write("`b `b") }
          } else { Invoke-UartForm '/uart/console/send' @{ format = 'hex'; data = '08' } | Out-Null }
          continue
        }
        if ($key.Key -eq [ConsoleKey]::Tab) {
          if ($localMode) { $localBuffer.Append("`t") | Out-Null; [Console]::Write("`t") }
          else { Invoke-UartForm '/uart/console/control' @{ code = 9 } | Out-Null }
          continue
        }
        if ($key.KeyChar -eq [char]0) { continue }

        if (!$localMode -and $key.KeyChar -eq '/') {
          $localMode = $true
          $localBuffer.Append('/') | Out-Null
          [Console]::Write('/')
        } elseif ($localMode) {
          $localBuffer.Append($key.KeyChar) | Out-Null
          [Console]::Write($key.KeyChar)
        } else {
          Invoke-UartForm '/uart/console/send' @{ format = 'text'; data = [string]$key.KeyChar } | Out-Null
        }
      }
      Start-Sleep -Milliseconds ([Math]::Max(20, $PollMs))
    }
  } finally { [Console]::TreatControlCAsInput = $oldCtrl }
}

$sessionStatus = Ensure-PowerShellSession

switch ($Action) {
  'console' { Start-InteractiveConsole }
  'status' {
    Write-Host ('PowerShell gekoppelt: {0} / Firmware {1}' -f $script:DeviceId, [string]$sessionStatus.firmware)
    Show-Object (Invoke-UartGet '/uart/status')
  }
  'console-read' { Show-Object (Invoke-UartGet ("/uart/console/data?since=$Since")) }
  'console-send' {
    $payload = $Text + (Get-LineEndingBytes $LineEnding)
    Invoke-UartForm '/uart/console/send' @{ format = 'text'; data = $payload } | Out-Null
    Write-Host 'OK'
  }
  'console-control' {
    $code = switch ($Control) { 'ctrl-c' { 3 } 'ctrl-d' { 4 } 'tab' { 9 } 'esc' { 27 } }
    Invoke-UartForm '/uart/console/control' @{ code = $code } | Out-Null
    Write-Host 'OK'
  }
  'heartbeat' {
    $enabled = if ($Disable) { '0' } else { '1' }
    if ($Enable) { $enabled = '1' }
    Show-Object (Invoke-UartForm '/uart/console/heartbeat' @{ enabled = $enabled })
  }
  'upload' { Show-Object (Start-UartUpload $LocalPath $TargetPath (-not $NoExec)) }
  'upload-status' { Show-Object (Get-UartUploadStatus) }
  'abort-upload' { Show-Object (Invoke-UartForm '/uart/upload/abort' @{}) }
  'download' { Show-Object (Start-UartDownload $Source $OutFile $StartBlock) }
  'download-status' { Show-Object (Get-UartDownloadStatus) }
  'abort-download' { Invoke-UartForm '/uart/image/abort' @{} | Out-Null; Write-Host 'OK' }
  'uart-start' { Invoke-UartForm '/uart/start' @{} | Out-Null; Write-Host 'OK' }
  'uart-stop' { Invoke-UartForm '/uart/stop' @{} | Out-Null; Write-Host 'OK' }
}
