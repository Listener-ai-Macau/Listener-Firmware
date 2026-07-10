[CmdletBinding(DefaultParameterSetName = 'Task')]
param(
  [Parameter(Mandatory = $true, ParameterSetName = 'Task')]
  [string]$Task,

  [Parameter(Mandatory = $true, ParameterSetName = 'TaskFile')]
  [string]$TaskFile,

  [Parameter(Mandatory = $true, ParameterSetName = 'PipelineFile')]
  [string]$PipelineFile,

  [Parameter(Mandatory = $true, ParameterSetName = 'PipelineJson')]
  [string]$PipelineJson,

  [Parameter(Mandatory = $true, ParameterSetName = 'ListLocks')]
  [switch]$ListLocks,

  [Parameter(Mandatory = $true, ParameterSetName = 'ClearStaleLocks')]
  [switch]$ClearStaleLocks,

  [string]$Name = 'task',
  [int]$TimeoutSeconds = 600,
  [switch]$Background,
  [switch]$InternalBackgroundWorker,
  [string]$JobId = '',
  [switch]$ForceForeground,
  [string]$Reason = '',
  [string[]]$Resource = @(),
  [switch]$AllowWorkspaceChanges,
  [switch]$NoWorktree,
  [switch]$KeepWorktree,
  [int]$BackgroundStartupTimeoutSeconds = 10,
  [ValidateSet('sonnet', 'haiku', 'opus', 'glm-5.1', 'glm-5-turbo', 'glm-4.7')]
  [string]$Model = '',
  [ValidateRange(0, 10)]
  [int]$MaxRetries = 1,
  [double]$MaxBudgetUsd = 10.0
)

if ($PSVersionTable.PSEdition -eq 'Desktop') {
  $pwshCommand = Get-Command pwsh.exe -ErrorAction SilentlyContinue
  if ($null -eq $pwshCommand) {
    throw 'claude_execute.ps1 requires pwsh.exe for reliable UTF-8 prompt handling.'
  }

  $forwardArgs = @('-NoProfile', '-File', $PSCommandPath)
  foreach ($entry in $PSBoundParameters.GetEnumerator()) {
    $nameArg = '-' + $entry.Key
    $value = $entry.Value
    if ($value -is [System.Management.Automation.SwitchParameter]) {
      if ($value.IsPresent) { $forwardArgs += $nameArg }
      continue
    }
    if ($null -eq $value) { continue }
    if ($value -is [array]) {
      foreach ($item in $value) { $forwardArgs += @($nameArg, [string]$item) }
      continue
    }
    $forwardArgs += @($nameArg, [string]$value)
  }

  & $pwshCommand.Source @forwardArgs
  exit $LASTEXITCODE
}

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = $utf8NoBom
[Console]::InputEncoding = $utf8NoBom
$OutputEncoding = $utf8NoBom
$env:PYTHONIOENCODING = 'utf-8'
$env:LANG = 'C.UTF-8'
$env:LC_ALL = 'C.UTF-8'

$script:LockPaths = @()
$script:WorktreePath = ''
$script:KeepWorktree = [bool]$KeepWorktree
$script:CleanupDone = $false

function Get-RepoRoot {
  return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Resolve-PowerShellHost {
  $pwsh = Get-Command pwsh.exe -ErrorAction SilentlyContinue
  if ($null -ne $pwsh) { return $pwsh.Source }
  throw 'pwsh.exe is required for reliable UTF-8 and validation behavior.'
}

function Resolve-ClaudePs1 {
  if (-not [string]::IsNullOrWhiteSpace($env:CLAUDE_EXECUTOR_CLAUDE_PS1)) {
    return (Resolve-Path -LiteralPath $env:CLAUDE_EXECUTOR_CLAUDE_PS1).Path
  }
  $cmd = Get-Command claude.ps1 -ErrorAction SilentlyContinue
  if ($null -ne $cmd) { return $cmd.Source }
  $cmd = Get-Command claude -ErrorAction SilentlyContinue
  if ($null -ne $cmd) { return $cmd.Source }
  throw 'claude executable not found. Install Claude Code or set CLAUDE_EXECUTOR_CLAUDE_PS1.'
}

function ConvertTo-SafeName([string]$Value) {
  $safe = ($Value -replace '[^A-Za-z0-9_.-]', '-').Trim('-')
  if ([string]::IsNullOrWhiteSpace($safe)) { return 'task' }
  if ($safe.Length -gt 48) { return $safe.Substring(0, 48) }
  return $safe
}

function ConvertTo-SafeResourceName([string]$Value) {
  return ConvertTo-SafeName ($Value.ToUpperInvariant())
}

function ConvertTo-Argument([string]$Value) {
  if ($null -eq $Value) { return '""' }
  if ($Value -match '^[A-Za-z0-9_./:=+,\\-]+$') { return $Value }
  return '"' + ($Value -replace '"', '\"') + '"'
}

function Join-Arguments([string[]]$Values) {
  return (($Values | ForEach-Object { ConvertTo-Argument $_ }) -join ' ')
}

function Get-ObjectPropertyValue($Object, [string]$Name) {
  if ($null -eq $Object) { return $null }
  if ($Object -is [System.Collections.IDictionary]) {
    if ($Object.Contains($Name)) { return $Object[$Name] }
    return $null
  }
  $prop = $Object.PSObject.Properties[$Name]
  if ($null -eq $prop) { return $null }
  return $prop.Value
}

function Write-StatusJson([string]$Path, $Value) {
  $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Write-ResultJson($Value) {
  Write-Output ("RESULT_JSON: " + ($Value | ConvertTo-Json -Depth 12 -Compress))
}

function Get-NormalizedResourceList([string[]]$Values) {
  $items = @()
  foreach ($value in @($Values)) {
    if ([string]::IsNullOrWhiteSpace($value)) { continue }
    foreach ($part in ($value -split '[,;]')) {
      $trimmed = $part.Trim()
      if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed -ieq 'NONE' -or $trimmed -ieq 'NULL') { continue }
      $items += ConvertTo-SafeResourceName $trimmed
    }
  }
  return @($items | Sort-Object -Unique)
}

function New-JobId([string]$Value) {
  return "$(Get-Date -Format 'yyyyMMdd-HHmmss')-$(ConvertTo-SafeName $Value)"
}

function Get-TaskText {
  switch ($PSCmdlet.ParameterSetName) {
    'TaskFile' { return Get-Content -LiteralPath (Resolve-Path -LiteralPath $TaskFile).Path -Raw }
    'PipelineFile' { return "Native pipeline file: $((Resolve-Path -LiteralPath $PipelineFile).Path)" }
    'PipelineJson' { return 'Native pipeline JSON supplied on command line.' }
    'ListLocks' { return 'List resource locks.' }
    'ClearStaleLocks' { return 'Clear stale resource locks.' }
    default { return $Task }
  }
}

function Get-Sha256Text([string]$Value) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    return [System.BitConverter]::ToString($sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Value))).Replace('-', '').ToLowerInvariant()
  } finally {
    $sha.Dispose()
  }
}

function Get-Sha256File([string]$Path) {
  if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
  try {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
  } catch {
    return ''
  }
}

function ConvertTo-NormalizedPathKey([string]$Path) {
  if ([string]::IsNullOrWhiteSpace($Path)) { return '' }
  try {
    return (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path.TrimEnd([char[]]@('\', '/')).ToLowerInvariant()
  } catch {
    return ([System.IO.Path]::GetFullPath($Path)).TrimEnd([char[]]@('\', '/')).ToLowerInvariant()
  }
}

function ConvertTo-BashPath([string]$Path) {
  if ([string]::IsNullOrWhiteSpace($Path)) { return '' }
  $full = try { (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path } catch { [System.IO.Path]::GetFullPath($Path) }
  $normalized = $full -replace '\\', '/'
  if ($normalized -match '^([A-Za-z]):/(.*)$') {
    return '/' + $matches[1].ToLowerInvariant() + '/' + $matches[2]
  }
  return $normalized
}

function Get-ChildProcessIds([int]$ParentProcessId) {
  try {
    return @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$ParentProcessId" -ErrorAction SilentlyContinue | ForEach-Object { [int]$_.ProcessId })
  } catch {
    return @()
  }
}

function Test-ProcessAlive([int]$ProcessId) {
  try {
    $proc = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    return ($null -ne $proc)
  } catch {
    return $false
  }
}

function Stop-ProcessTree([System.Diagnostics.Process]$Process) {
  if ($null -eq $Process) { return }
  try { if ($Process.HasExited) { return } } catch { return }
  $ids = @(@(Get-ChildProcessIds ([int]$Process.Id)) + @([int]$Process.Id) | Select-Object -Unique)
  foreach ($idToStop in $ids) {
    Stop-Process -Id $idToStop -Force -ErrorAction SilentlyContinue
  }
}

function Limit-LogText([string]$Text, [int]$MaxLines = 80) {
  if ([string]::IsNullOrWhiteSpace($Text)) { return '' }
  $lines = @($Text -split '\r?\n')
  if ($lines.Count -le $MaxLines) { return $Text.Trim() }
  $head = @($lines | Select-Object -First ([Math]::Floor($MaxLines / 2)))
  $tail = @($lines | Select-Object -Last ([Math]::Ceiling($MaxLines / 2)))
  return (@($head + @("... truncated $($lines.Count - $MaxLines) lines ...") + $tail) -join [char]10).Trim()
}

function Get-TaskResultOrDefault($Task, [int]$TimeoutMs) {
  if ($null -eq $Task) { return '' }
  try {
    if ($Task.Wait($TimeoutMs)) { return [string]$Task.Result }
  } catch {}
  return ''
}

function Set-ProcessEncodingIfAvailable($StartInfo, [string]$PropertyName, [System.Text.Encoding]$Encoding) {
  $property = $StartInfo.GetType().GetProperty($PropertyName)
  if ($null -eq $property -or -not $property.CanWrite) { return }
  $property.SetValue($StartInfo, $Encoding, $null)
}

function Invoke-ProcessCommand([string]$FilePath, [string[]]$Arguments, [string]$WorkDir, [int]$TimeoutSec) {
  $process = [System.Diagnostics.Process]::new()
  $process.StartInfo.FileName = $FilePath
  $process.StartInfo.Arguments = Join-Arguments $Arguments
  $process.StartInfo.WorkingDirectory = $WorkDir
  $process.StartInfo.UseShellExecute = $false
  $process.StartInfo.RedirectStandardInput = $false
  $process.StartInfo.RedirectStandardOutput = $true
  $process.StartInfo.RedirectStandardError = $true
  $process.StartInfo.CreateNoWindow = $true
  $process.StartInfo.EnvironmentVariables['PYTHONIOENCODING'] = 'utf-8'
  $process.StartInfo.EnvironmentVariables['LANG'] = 'C.UTF-8'
  $process.StartInfo.EnvironmentVariables['LC_ALL'] = 'C.UTF-8'
  Set-ProcessEncodingIfAvailable $process.StartInfo 'StandardOutputEncoding' $utf8NoBom
  Set-ProcessEncodingIfAvailable $process.StartInfo 'StandardErrorEncoding' $utf8NoBom

  $started = Get-Date
  $timedOut = $false
  $exitCode = $null
  $errorMessage = $null
  $stdoutText = ''
  $stderrText = ''
  try {
    [void]$process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit([Math]::Max(1, $TimeoutSec) * 1000)) {
      $timedOut = $true
      try { Stop-ProcessTree $process } catch { $errorMessage = $_.Exception.Message }
      [void]$process.WaitForExit(5000)
    } else {
      $process.WaitForExit()
    }
    try { $exitCode = $process.ExitCode } catch { $exitCode = $null }
    $stdoutText = Get-TaskResultOrDefault $stdoutTask 5000
    $stderrText = Get-TaskResultOrDefault $stderrTask 5000
  } catch {
    $errorMessage = $_.Exception.Message
  }
  $ended = Get-Date
  return [ordered]@{
    timed_out = $timedOut
    exit_code = $exitCode
    error = $errorMessage
    stdout = $stdoutText
    stderr = $stderrText
    duration_ms = [int](($ended - $started).TotalMilliseconds)
  }
}

function Get-ResourceLockStaleMinutes { return 60 }

function Get-ResourceLockEntries([string]$LockRoot) {
  if (-not (Test-Path -LiteralPath $LockRoot -PathType Container)) { return @() }
  $entries = @()
  foreach ($file in @(Get-ChildItem -LiteralPath $LockRoot -Filter '*.lock.json' -File -ErrorAction SilentlyContinue)) {
    try {
      $json = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
      $entries += [pscustomobject]@{
        resource = [string](Get-ObjectPropertyValue $json 'resource')
        id = [string](Get-ObjectPropertyValue $json 'id')
        process_id = Get-ObjectPropertyValue $json 'process_id'
        artifact_dir = [string](Get-ObjectPropertyValue $json 'artifact_dir')
        acquired_time = [string](Get-ObjectPropertyValue $json 'acquired_time')
        path = $file.FullName
      }
    } catch {
      $entries += [pscustomobject]@{ resource = $file.BaseName -replace '\.lock$', ''; id = 'unparseable'; process_id = $null; artifact_dir = ''; acquired_time = ''; path = $file.FullName }
    }
  }
  return @($entries)
}

function Clear-DeadOrStaleLock([string]$LockPath, $ExistingLock) {
  $ageMinutes = 0
  try { $ageMinutes = ((Get-Date) - (Get-Item -LiteralPath $LockPath).LastWriteTime).TotalMinutes } catch { $ageMinutes = 0 }
  if ($ageMinutes -gt (Get-ResourceLockStaleMinutes)) {
    Remove-Item -LiteralPath $LockPath -Force -ErrorAction SilentlyContinue
    return $true
  }
  $ownerPid = Get-ObjectPropertyValue $ExistingLock 'process_id'
  if ($null -eq $ownerPid) { return $false }
  try { $pidValue = [int]$ownerPid } catch { return $false }
  if (-not (Test-ProcessAlive $pidValue)) {
    Remove-Item -LiteralPath $LockPath -Force -ErrorAction SilentlyContinue
    return $true
  }
  return $false
}

function Acquire-ResourceLocks([string[]]$Resources, [string]$LockRoot, [string]$OwnerId, [string]$ArtifactDir) {
  $locks = @()
  $normalized = @(Get-NormalizedResourceList $Resources)
  if ($normalized.Count -eq 0) { return $locks }
  New-Item -ItemType Directory -Path $LockRoot -Force | Out-Null
  try {
    foreach ($resourceName in $normalized) {
      $lockPath = Join-Path $LockRoot "$resourceName.lock.json"
      while ($true) {
        if (Test-Path -LiteralPath $lockPath) {
          $existing = $null
          try { $existing = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json } catch { $existing = $null }
          if ($null -ne $existing -and (Clear-DeadOrStaleLock $lockPath $existing)) { continue }
          $ownerText = if ($null -ne $existing) { "$($existing.id) pid=$($existing.process_id)" } else { 'unknown owner' }
          throw "resource '$resourceName' is locked by $ownerText at $lockPath"
        }
        $payload = [ordered]@{ resource = $resourceName; id = $OwnerId; process_id = $PID; artifact_dir = $ArtifactDir; acquired_time = (Get-Date -Format o) }
        try {
          New-Item -ItemType File -Path $lockPath -Value ($payload | ConvertTo-Json -Depth 6) -ErrorAction Stop | Out-Null
          $locks += $lockPath
          break
        } catch {
          Start-Sleep -Milliseconds 150
        }
      }
    }
  } catch {
    Release-ResourceLocks $locks $OwnerId
    throw
  }
  return $locks
}

function Release-ResourceLocks([string[]]$LockPaths, [string]$OwnerId) {
  foreach ($lockPath in @($LockPaths)) {
    if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) { continue }
    try {
      $existing = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
      if ([string](Get-ObjectPropertyValue $existing 'id') -eq $OwnerId) {
        Remove-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue
      }
    } catch {
      Remove-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue
    }
  }
}

function Release-ResourceLocksByOwner([string]$LockRoot, [string[]]$Resources, [string]$OwnerId) {
  foreach ($resourceName in @(Get-NormalizedResourceList $Resources)) {
    $lockPath = Join-Path $LockRoot "$resourceName.lock.json"
    if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) { continue }
    try {
      $existing = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
      if ([string](Get-ObjectPropertyValue $existing 'id') -eq $OwnerId) {
        Remove-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue
      }
    } catch {
      Remove-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue
    }
  }
}

function Get-ExecutorWorktreeRoot([string]$RepoRoot) {
  $repoHash = (Get-Sha256Text $RepoRoot).Substring(0, 16)
  return Join-Path ([System.IO.Path]::GetTempPath()) "codex_claude_executor_worktrees\$repoHash"
}

function Assert-GitTopLevel([string]$Path) {
  $resolved = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
  $top = ((& git -C $resolved rev-parse --show-toplevel 2>&1) -join [char]10).Trim()
  if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($top)) {
    throw "not a git worktree: $resolved"
  }
  if ((ConvertTo-NormalizedPathKey $top) -ne (ConvertTo-NormalizedPathKey $resolved)) {
    throw "git top-level mismatch: expected '$resolved', got '$top'"
  }
}

function Sync-WorkspaceSnapshotToWorktree([string]$RepoRoot, [string]$WorktreePath) {
  $trackedCopied = @()
  $untrackedCopied = @()
  Push-Location -LiteralPath $RepoRoot
  try {
    foreach ($path in @(& git diff --name-only HEAD -- . ':(exclude).cache' 2>$null)) {
      if ([string]::IsNullOrWhiteSpace($path)) { continue }
      $src = Join-Path $RepoRoot $path
      $dst = Join-Path $WorktreePath $path
      if (Test-Path -LiteralPath $src -PathType Leaf) {
        $dstParent = Split-Path -Parent $dst
        if (-not [string]::IsNullOrWhiteSpace($dstParent)) { New-Item -ItemType Directory -Path $dstParent -Force | Out-Null }
        Copy-Item -LiteralPath $src -Destination $dst -Force
        $trackedCopied += $path
      } elseif (-not (Test-Path -LiteralPath $src)) {
        Remove-Item -LiteralPath $dst -Force -ErrorAction SilentlyContinue
        $trackedCopied += "$path|deleted"
      }
    }

    foreach ($path in @(& git ls-files --others --exclude-standard 2>$null)) {
      if ([string]::IsNullOrWhiteSpace($path) -or $path -like '.cache/*' -or $path -like '.cache\*') { continue }
      $src = Join-Path $RepoRoot $path
      $dst = Join-Path $WorktreePath $path
      if (Test-Path -LiteralPath $src -PathType Container) {
        New-Item -ItemType Directory -Path $dst -Force | Out-Null
        continue
      }
      if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { continue }
      $dstParent = Split-Path -Parent $dst
      if (-not [string]::IsNullOrWhiteSpace($dstParent)) { New-Item -ItemType Directory -Path $dstParent -Force | Out-Null }
      Copy-Item -LiteralPath $src -Destination $dst -Force
      $untrackedCopied += $path
    }
  } finally {
    Pop-Location
  }
  return [ordered]@{ tracked_copied = @($trackedCopied); untracked_copied = @($untrackedCopied) }
}

function Get-GitWorkspaceSnapshot([string]$RepoRoot) {
  $snapshot = [ordered]@{
    available = $false
    root = $RepoRoot
    head = ''
    branch = ''
    diff_hash = ''
    status_hash = ''
    changed_paths = @()
    status = @()
    ignored_paths = @('.cache', 'tests/artifacts')
    error = ''
  }
  if ([string]::IsNullOrWhiteSpace($RepoRoot) -or -not (Test-Path -LiteralPath $RepoRoot -PathType Container)) {
    $snapshot.error = 'workspace root is unavailable'
    return $snapshot
  }
  Push-Location -LiteralPath $RepoRoot
  try {
    $head = ((& git rev-parse HEAD 2>&1) -join [char]10).Trim()
    if ($LASTEXITCODE -ne 0) { throw "git rev-parse HEAD failed: $head" }
    $branch = ((& git rev-parse --abbrev-ref HEAD 2>$null) -join [char]10).Trim()
    $pathspec = @('.', ':(exclude).cache', ':(exclude)tests/artifacts')
    $diffText = ((& git diff --no-ext-diff --binary -- @pathspec 2>&1) -join [char]10)
    if ($LASTEXITCODE -ne 0) { throw "git diff failed: $diffText" }
    $statusLines = @(& git status --porcelain=v1 --untracked-files=all -- @pathspec 2>$null)
    $changedPaths = @()
    foreach ($line in @($statusLines)) {
      if ([string]::IsNullOrWhiteSpace($line)) { continue }
      $pathText = if ($line.Length -ge 4) { $line.Substring(3) } else { $line }
      $changedPaths += $pathText
    }
    $snapshot.available = $true
    $snapshot.head = $head
    $snapshot.branch = $branch
    $snapshot.diff_hash = Get-Sha256Text $diffText
    $snapshot.status_hash = Get-Sha256Text ($statusLines -join [char]10)
    $snapshot.changed_paths = @($changedPaths | Sort-Object -Unique)
    $snapshot.status = @($statusLines)
  } catch {
    $snapshot.error = $_.Exception.Message
  } finally {
    Pop-Location
  }
  return $snapshot
}

function Get-FileArtifactSnapshot([string]$Root, [string]$RelativePath) {
  $path = Join-Path $Root $RelativePath
  $exists = Test-Path -LiteralPath $path -PathType Leaf
  $item = if ($exists) { Get-Item -LiteralPath $path -ErrorAction SilentlyContinue } else { $null }
  return [ordered]@{
    path = $path
    relative_path = $RelativePath
    exists = [bool]$exists
    size_bytes = if ($null -eq $item) { $null } else { [int64]$item.Length }
    sha256 = if ($exists) { Get-Sha256File $path } else { '' }
    last_write_time = if ($null -eq $item) { '' } else { $item.LastWriteTimeUtc.ToString('o') }
  }
}

function Get-ArtifactFilesSnapshot([string]$Root, [array]$ArtifactSpecs) {
  $items = @()
  foreach ($spec in @($ArtifactSpecs)) {
    $relative = [string](Get-ObjectPropertyValue $spec 'path')
    if ([string]::IsNullOrWhiteSpace($relative)) { continue }
    $snapshot = Get-FileArtifactSnapshot $Root $relative
    $snapshot.label = [string](Get-ObjectPropertyValue $spec 'label')
    $snapshot.required = [bool](Get-ObjectPropertyValue $spec 'required')
    $items += $snapshot
  }
  return @($items)
}

function Get-ConsistencySnapshot([string]$Root, [array]$ArtifactSpecs = @()) {
  return [ordered]@{
    source = Get-GitWorkspaceSnapshot $Root
    artifacts = Get-ArtifactFilesSnapshot $Root $ArtifactSpecs
  }
}

function Get-WorkspaceDelta($Before, $After) {
  $changed = $false
  if ($null -eq $Before -or $null -eq $After) {
    $changed = $false
  } elseif ([string](Get-ObjectPropertyValue $Before 'head') -ne [string](Get-ObjectPropertyValue $After 'head')) {
    $changed = $true
  } elseif ([string](Get-ObjectPropertyValue $Before 'diff_hash') -ne [string](Get-ObjectPropertyValue $After 'diff_hash')) {
    $changed = $true
  } elseif ([string](Get-ObjectPropertyValue $Before 'status_hash') -ne [string](Get-ObjectPropertyValue $After 'status_hash')) {
    $changed = $true
  }
  return [ordered]@{
    changed = [bool]$changed
    changed_paths = if ($null -eq $After) { @() } else { @((Get-ObjectPropertyValue $After 'changed_paths')) }
    before = $Before
    after = $After
  }
}

function Resolve-StepOutputPath([string]$WorkDir, [string]$Value) {
  $trimmed = ([string]$Value).Trim().Trim('"').Trim("'")
  if ([string]::IsNullOrWhiteSpace($trimmed)) { return '' }
  if ([System.IO.Path]::IsPathRooted($trimmed)) { return $trimmed }
  return Join-Path $WorkDir $trimmed
}

function Get-JsonFieldValue($Object, [string]$Path) {
  if ($null -eq $Object -or [string]::IsNullOrWhiteSpace($Path)) { return $null }
  $cursor = $Object
  foreach ($part in ($Path -split '\.')) {
    if ([string]::IsNullOrWhiteSpace($part)) { continue }
    $cursor = Get-ObjectPropertyValue $cursor $part
    if ($null -eq $cursor) { return $null }
  }
  return $cursor
}

function Test-StructuredResultRule($Json, $Rule) {
  $field = [string](Get-ObjectPropertyValue $Rule 'field')
  if ([string]::IsNullOrWhiteSpace($field)) { return $false }
  $actual = Get-JsonFieldValue $Json $field
  $equals = Get-ObjectPropertyValue $Rule 'equals'
  if ($null -ne $equals -and [string]$actual -ne [string]$equals) { return $false }
  $notEquals = Get-ObjectPropertyValue $Rule 'not_equals'
  if ($null -ne $notEquals -and [string]$actual -eq [string]$notEquals) { return $false }
  $greaterThan = Get-ObjectPropertyValue $Rule 'greater_than'
  if ($null -ne $greaterThan) {
    try {
      if ([double]$actual -le [double]$greaterThan) { return $false }
    } catch {
      return $false
    }
  }
  $lessThan = Get-ObjectPropertyValue $Rule 'less_than'
  if ($null -ne $lessThan) {
    try {
      if ([double]$actual -ge [double]$lessThan) { return $false }
    } catch {
      return $false
    }
  }
  return ($null -ne $equals -or $null -ne $notEquals -or $null -ne $greaterThan -or $null -ne $lessThan)
}

function Get-StructuredResultRuleSignal($Json, [array]$Rules) {
  foreach ($rule in @($Rules)) {
    if (-not (Test-StructuredResultRule $Json $rule)) { continue }
    $status = [string](Get-ObjectPropertyValue $rule 'status')
    if ([string]::IsNullOrWhiteSpace($status)) { $status = 'INCONCLUSIVE' }
    $status = $status.ToUpperInvariant()
    $reason = [string](Get-ObjectPropertyValue $rule 'reason')
    if ([string]::IsNullOrWhiteSpace($reason)) {
      $reason = "structured result rule matched: $([string](Get-ObjectPropertyValue $rule 'field'))"
    }
    return [ordered]@{ status = $status; reason = $reason; rule = $rule }
  }
  return [ordered]@{ status = 'PASS'; reason = ''; rule = $null }
}

function Copy-StructuredResultPathFields($Json, [array]$FieldNames, [string]$WorkDir, [string]$CopyDir, [int]$Index) {
  $copies = [ordered]@{}
  if ([string]::IsNullOrWhiteSpace($CopyDir) -or -not (Test-Path -LiteralPath $CopyDir -PathType Container)) { return $copies }
  foreach ($field in @($FieldNames)) {
    $value = [string](Get-JsonFieldValue $Json $field)
    if ([string]::IsNullOrWhiteSpace($value)) { continue }
    $path = Resolve-StepOutputPath $WorkDir $value
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    $extension = [System.IO.Path]::GetExtension($path)
    if ([string]::IsNullOrWhiteSpace($extension)) { $extension = '.artifact' }
    $copyPath = Join-Path $CopyDir ('structured_result_{0:00}_{1}{2}' -f $Index, (ConvertTo-SafeName $field), $extension)
    Copy-Item -LiteralPath $path -Destination $copyPath -Force -ErrorAction SilentlyContinue
    $copies[$field] = [ordered]@{ path = $path; artifact_copy_path = $copyPath }
  }
  return $copies
}

function Get-StructuredResultReferences([string]$WorkDir, [string]$StdoutText, [string]$StderrText, [array]$Specs, [string]$CopyDir = '') {
  $text = ([string]$StdoutText) + [char]10 + ([string]$StderrText)
  $items = @()
  $index = 0
  foreach ($spec in @($Specs)) {
    if ($null -eq $spec) { continue }
    $name = [string](Get-ObjectPropertyValue $spec 'name')
    if ([string]::IsNullOrWhiteSpace($name)) { $name = 'result' }
    $paths = @()
    $explicitPath = [string](Get-ObjectPropertyValue $spec 'path')
    if (-not [string]::IsNullOrWhiteSpace($explicitPath)) {
      $paths += Resolve-StepOutputPath $WorkDir $explicitPath
    }
    $stdoutRegex = [string](Get-ObjectPropertyValue $spec 'stdout_regex')
    if (-not [string]::IsNullOrWhiteSpace($stdoutRegex)) {
      foreach ($match in [regex]::Matches($text, $stdoutRegex)) {
        if ($match.Groups.Count -gt 1) {
          $paths += Resolve-StepOutputPath $WorkDir $match.Groups[1].Value
        }
      }
    }
    if ([string]::IsNullOrWhiteSpace($explicitPath) -and [string]::IsNullOrWhiteSpace($stdoutRegex)) {
      continue
    }
    $uniquePaths = @($paths | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
    if ($uniquePaths.Count -eq 0) {
      $index++
      $items += [ordered]@{
        name = $name
        path = if ([string]::IsNullOrWhiteSpace($stdoutRegex)) { $explicitPath } else { "stdout_regex:$stdoutRegex" }
        artifact_copy_path = ''
        exists = $false
        sha256 = ''
        summary = [ordered]@{}
        copied_paths = [ordered]@{}
        rule_signal = [ordered]@{ status = [string](Get-ObjectPropertyValue $spec 'missing_status'); reason = 'structured result JSON was not found'; rule = $null }
      }
      continue
    }
    foreach ($path in $uniquePaths) {
      $index++
      $exists = Test-Path -LiteralPath $path -PathType Leaf
      $copyPath = ''
      $summary = [ordered]@{}
      $copiedPaths = [ordered]@{}
      $ruleSignal = [ordered]@{ status = 'PASS'; reason = ''; rule = $null }
      if ($exists) {
        try {
          $json = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
          foreach ($field in @((Get-ObjectPropertyValue $spec 'summary_fields'))) {
            if ([string]::IsNullOrWhiteSpace([string]$field)) { continue }
            $summary[[string]$field] = Get-JsonFieldValue $json ([string]$field)
          }
          if ($summary.Count -eq 0) {
            $summary.status = Get-JsonFieldValue $json 'status'
          }
          $copiedPaths = Copy-StructuredResultPathFields $json @((Get-ObjectPropertyValue $spec 'copy_path_fields')) $WorkDir $CopyDir $index
          $ruleSignal = Get-StructuredResultRuleSignal $json @((Get-ObjectPropertyValue $spec 'rules'))
        } catch {
          $summary = [ordered]@{ error = $_.Exception.Message }
          $ruleSignal = [ordered]@{ status = 'INCONCLUSIVE'; reason = "structured result JSON could not be parsed: $($_.Exception.Message)"; rule = $null }
        }
        if (-not [string]::IsNullOrWhiteSpace($CopyDir) -and (Test-Path -LiteralPath $CopyDir -PathType Container)) {
          $copyPath = Join-Path $CopyDir ('structured_result_{0:00}_{1}.json' -f $index, (ConvertTo-SafeName $name))
          Copy-Item -LiteralPath $path -Destination $copyPath -Force -ErrorAction SilentlyContinue
        }
      }
      $items += [ordered]@{
        name = $name
        path = $path
        artifact_copy_path = $copyPath
        exists = [bool]$exists
        sha256 = if ($exists) { Get-Sha256File $path } else { '' }
        summary = $summary
        copied_paths = $copiedPaths
        rule_signal = $ruleSignal
      }
    }
  }
  return @($items)
}

function Get-StructuredResultSignal([array]$StructuredResults) {
  foreach ($item in @($StructuredResults)) {
    if (-not [bool](Get-ObjectPropertyValue $item 'exists')) {
      $missingPath = [string](Get-ObjectPropertyValue $item 'path')
      $missingSignal = Get-ObjectPropertyValue $item 'rule_signal'
      $missingStatus = [string](Get-ObjectPropertyValue $missingSignal 'status')
      if ([string]::IsNullOrWhiteSpace($missingStatus)) { $missingStatus = 'INCONCLUSIVE' }
      return [ordered]@{ status = $missingStatus.ToUpperInvariant(); reason = "structured result JSON was referenced but not found: $missingPath" }
    }
    $signal = Get-ObjectPropertyValue $item 'rule_signal'
    $status = [string](Get-ObjectPropertyValue $signal 'status')
    if ([string]::IsNullOrWhiteSpace($status)) { $status = 'PASS' }
    if ($status.ToUpperInvariant() -ne 'PASS') {
      return [ordered]@{ status = $status.ToUpperInvariant(); reason = [string](Get-ObjectPropertyValue $signal 'reason') }
    }
  }
  return [ordered]@{ status = 'PASS'; reason = '' }
}

function Get-PipelinePolicyReport($PipelineResult) {
  $issues = @()
  $previousDiffHash = ''
  $previousHead = ''
  $stepCount = 0
  foreach ($step in @($PipelineResult.steps)) {
    $stepCount++
    $consistency = Get-ObjectPropertyValue $step 'consistency'
    $after = Get-ObjectPropertyValue $consistency 'after'
    $source = Get-ObjectPropertyValue $after 'source'
    $head = [string](Get-ObjectPropertyValue $source 'head')
    $diffHash = [string](Get-ObjectPropertyValue $source 'diff_hash')
    if ($stepCount -gt 1 -and -not [string]::IsNullOrWhiteSpace($previousHead) -and $head -ne $previousHead) {
      $issues += [ordered]@{ severity = 'INCONCLUSIVE'; step = [int]$step.index; code = 'source_head_changed'; reason = "source HEAD changed between pipeline steps: $previousHead -> $head" }
    }
    if ($stepCount -gt 1 -and -not [string]::IsNullOrWhiteSpace($previousDiffHash) -and $diffHash -ne $previousDiffHash) {
      $issues += [ordered]@{ severity = 'INCONCLUSIVE'; step = [int]$step.index; code = 'source_diff_hash_changed'; reason = "source diff_hash changed between pipeline steps: $previousDiffHash -> $diffHash" }
    }
    $previousHead = $head
    $previousDiffHash = $diffHash

    foreach ($structuredResult in @((Get-ObjectPropertyValue $consistency 'structured_results'))) {
      $name = [string](Get-ObjectPropertyValue $structuredResult 'name')
      if ([string]::IsNullOrWhiteSpace($name)) { $name = 'result' }
      if (-not [bool](Get-ObjectPropertyValue $structuredResult 'exists')) {
        $issues += [ordered]@{ severity = 'INCONCLUSIVE'; step = [int]$step.index; code = 'structured_result_missing'; reason = "structured result '$name' missing: $([string](Get-ObjectPropertyValue $structuredResult 'path'))" }
      }
      $copyPath = [string](Get-ObjectPropertyValue $structuredResult 'artifact_copy_path')
      if ([bool](Get-ObjectPropertyValue $structuredResult 'exists') -and [string]::IsNullOrWhiteSpace($copyPath)) {
        $issues += [ordered]@{ severity = 'INCONCLUSIVE'; step = [int]$step.index; code = 'structured_result_not_copied'; reason = "structured result '$name' exists but has no artifact copy path" }
      }
      $signal = Get-ObjectPropertyValue $structuredResult 'rule_signal'
      $signalStatus = ([string](Get-ObjectPropertyValue $signal 'status')).ToUpperInvariant()
      if ($signalStatus -eq 'FAIL') {
        $issues += [ordered]@{ severity = 'FAIL'; step = [int]$step.index; code = 'structured_result_fail'; reason = [string](Get-ObjectPropertyValue $signal 'reason') }
      } elseif ($signalStatus -eq 'INCONCLUSIVE') {
        $issues += [ordered]@{ severity = 'INCONCLUSIVE'; step = [int]$step.index; code = 'structured_result_inconclusive'; reason = [string](Get-ObjectPropertyValue $signal 'reason') }
      }
    }
  }
  $policyStatus = if (@($issues | Where-Object { $_.severity -eq 'FAIL' }).Count -gt 0) {
    'FAIL'
  } elseif (@($issues | Where-Object { $_.severity -eq 'INCONCLUSIVE' }).Count -gt 0) {
    'INCONCLUSIVE'
  } else {
    'PASS'
  }
  return [ordered]@{
    status = $policyStatus
    continue_ok = ($policyStatus -eq 'PASS' -and [string]$PipelineResult.status -eq 'PASS')
    issues = @($issues)
  }
}

function New-ExecutorWorktree([string]$RepoRoot, [string]$JobId) {
  $git = Get-Command git -ErrorAction SilentlyContinue
  if ($null -eq $git) { throw 'git unavailable; cannot create executor worktree' }
  $root = Get-ExecutorWorktreeRoot $RepoRoot
  New-Item -ItemType Directory -Path $root -Force | Out-Null
  $path = Join-Path $root $JobId
  if (Test-Path -LiteralPath $path) { throw "executor worktree already exists: $path" }

  Push-Location -LiteralPath $RepoRoot
  try {
    $head = ((& git rev-parse HEAD 2>&1) -join [char]10).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($head)) { throw "git rev-parse HEAD failed: $head" }
    $output = ((& git worktree add --detach $path HEAD 2>&1) -join [char]10)
    if ($LASTEXITCODE -ne 0) { throw "git worktree add failed: $output" }
  } finally {
    Pop-Location
  }

  try {
    Assert-GitTopLevel $path
    $snapshot = Sync-WorkspaceSnapshotToWorktree $RepoRoot $path
    return [ordered]@{ enabled = $true; path = $path; head = $head; root = $root; snapshot = $snapshot }
  } catch {
    Remove-ExecutorWorktree $RepoRoot $path $false
    throw
  }
}

function Remove-ExecutorWorktree([string]$RepoRoot, [string]$WorktreePath, [bool]$ThrowOnMismatch = $true) {
  if ([string]::IsNullOrWhiteSpace($WorktreePath)) { return }
  $resolved = ''
  try { $resolved = (Resolve-Path -LiteralPath $WorktreePath -ErrorAction Stop).Path } catch { return }
  $root = Get-ExecutorWorktreeRoot $RepoRoot
  $rootPath = (Resolve-Path -LiteralPath $root -ErrorAction SilentlyContinue)
  if ($null -eq $rootPath -or -not $resolved.StartsWith($rootPath.Path, [System.StringComparison]::OrdinalIgnoreCase)) {
    if ($ThrowOnMismatch) { throw "refusing to remove worktree outside executor temp root: $resolved" }
    return
  }

  try {
    Assert-GitTopLevel $resolved
  } catch {
    if ($ThrowOnMismatch) { throw }
    return
  }

  Push-Location -LiteralPath $RepoRoot
  try {
    & git worktree remove --force $resolved 2>$null | Out-Null
    & git worktree prune 2>$null | Out-Null
  } finally {
    Pop-Location
  }
  if (Test-Path -LiteralPath $resolved) {
    Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
  }
}

function ConvertTo-StringArray($Value) {
  if ($null -eq $Value) { return @() }
  $items = @()
  $values = if ($Value -is [array]) { @($Value) } else { @($Value) }
  foreach ($entry in $values) {
    if ($null -eq $entry) { continue }
    if ($entry -is [string]) {
      if (-not [string]::IsNullOrWhiteSpace($entry)) { $items += [string]$entry }
      continue
    }
    $regex = [string](Get-ObjectPropertyValue $entry 'regex')
    if ([string]::IsNullOrWhiteSpace($regex)) { $regex = [string](Get-ObjectPropertyValue $entry 'pattern') }
    if (-not [string]::IsNullOrWhiteSpace($regex)) { $items += $regex }
  }
  return @($items)
}

function ConvertTo-ArgumentArray($Value) {
  if ($null -eq $Value) { return @() }
  $values = if ($Value -is [array]) { @($Value) } else { @($Value) }
  $items = @()
  foreach ($entry in $values) {
    if ($null -eq $entry) { continue }
    $items += [string]$entry
  }
  return @($items)
}

function ConvertTo-MatchRuleArray($Value, [string]$DefaultStatus, [string]$Target) {
  if ($null -eq $Value) { return @() }
  $values = if ($Value -is [array]) { @($Value) } else { @($Value) }
  $rules = @()
  foreach ($entry in $values) {
    if ($null -eq $entry) { continue }
    if ($entry -is [string]) {
      if (-not [string]::IsNullOrWhiteSpace($entry)) {
        $rules += [pscustomobject]@{ status = $DefaultStatus; target = $Target; regex = [string]$entry; reason = '' }
      }
      continue
    }
    $regex = [string](Get-ObjectPropertyValue $entry 'regex')
    if ([string]::IsNullOrWhiteSpace($regex)) { $regex = [string](Get-ObjectPropertyValue $entry 'pattern') }
    if ([string]::IsNullOrWhiteSpace($regex)) { continue }
    $status = [string](Get-ObjectPropertyValue $entry 'status')
    if ([string]::IsNullOrWhiteSpace($status)) { $status = $DefaultStatus }
    $status = $status.ToUpperInvariant()
    if ($status -notin @('FAIL', 'INCONCLUSIVE')) { throw "native pipeline rule status must be FAIL or INCONCLUSIVE, got '$status'" }
    $targetValue = [string](Get-ObjectPropertyValue $entry 'target')
    if ([string]::IsNullOrWhiteSpace($targetValue)) { $targetValue = $Target }
    $targetValue = $targetValue.ToLowerInvariant()
    if ($targetValue -notin @('stdout', 'stderr', 'output')) { throw "native pipeline rule target must be stdout, stderr, or output, got '$targetValue'" }
    $reason = [string](Get-ObjectPropertyValue $entry 'reason')
    $rules += [pscustomobject]@{ status = $status; target = $targetValue; regex = $regex; reason = $reason }
  }
  return @($rules)
}

function Get-NativePipelineSteps {
  if ($PSCmdlet.ParameterSetName -eq 'PipelineFile') {
    $text = Get-Content -LiteralPath (Resolve-Path -LiteralPath $PipelineFile).Path -Raw
  } elseif ($PSCmdlet.ParameterSetName -eq 'PipelineJson') {
    $text = $PipelineJson
  } else {
    return @()
  }

  $parsed = $text | ConvertFrom-Json
  $stepsProp = Get-ObjectPropertyValue $parsed 'steps'
  $steps = if ($parsed -is [array]) { @($parsed) } elseif ($null -ne $stepsProp) { @($stepsProp) } else { @($parsed) }
  if ($steps.Count -eq 0) { throw 'native pipeline requires a non-empty JSON array or an object with steps[]' }

  $normalized = @()
  $index = 0
  foreach ($step in $steps) {
    $index++
    $name = [string](Get-ObjectPropertyValue $step 'name')
    $command = [string](Get-ObjectPropertyValue $step 'command')
    $args = @(ConvertTo-ArgumentArray (Get-ObjectPropertyValue $step 'args'))
    $timeoutValue = Get-ObjectPropertyValue $step 'timeout_seconds'
    if ([string]::IsNullOrWhiteSpace($name)) { $name = "step_$index" }
    if ([string]::IsNullOrWhiteSpace($command)) { throw "native pipeline step $index is missing command" }
    $stepTimeout = $TimeoutSeconds
    if ($null -ne $timeoutValue) {
      try { $stepTimeout = [int]$timeoutValue } catch { throw "native pipeline step $index has invalid timeout_seconds" }
      if ($stepTimeout -le 0) { throw "native pipeline step $index timeout_seconds must be positive" }
    }

    $existingRules = @(ConvertTo-MatchRuleArray (Get-ObjectPropertyValue $step 'match_rules') 'INCONCLUSIVE' 'output')
    if ($existingRules.Count -gt 0) {
      $rules = @($existingRules)
    } else {
      $rules = @()
      $rules += ConvertTo-MatchRuleArray (Get-ObjectPropertyValue $step 'fail_on_stdout_regex') 'FAIL' 'stdout'
      $rules += ConvertTo-MatchRuleArray (Get-ObjectPropertyValue $step 'fail_on_stderr_regex') 'FAIL' 'stderr'
      $rules += ConvertTo-MatchRuleArray (Get-ObjectPropertyValue $step 'fail_on_output_regex') 'FAIL' 'output'
      $rules += ConvertTo-MatchRuleArray (Get-ObjectPropertyValue $step 'inconclusive_on_stdout_regex') 'INCONCLUSIVE' 'stdout'
      $rules += ConvertTo-MatchRuleArray (Get-ObjectPropertyValue $step 'inconclusive_on_stderr_regex') 'INCONCLUSIVE' 'stderr'
      $rules += ConvertTo-MatchRuleArray (Get-ObjectPropertyValue $step 'inconclusive_on_output_regex') 'INCONCLUSIVE' 'output'
    }

    $normalized += [pscustomobject]@{
      index = $index
      name = $name
      command = $command
      args = @($args)
      timeout_seconds = $stepTimeout
      fail_on_stdout_regex = @(ConvertTo-StringArray (Get-ObjectPropertyValue $step 'fail_on_stdout_regex'))
      fail_on_stderr_regex = @(ConvertTo-StringArray (Get-ObjectPropertyValue $step 'fail_on_stderr_regex'))
      fail_on_output_regex = @(ConvertTo-StringArray (Get-ObjectPropertyValue $step 'fail_on_output_regex'))
      inconclusive_on_stdout_regex = @(ConvertTo-StringArray (Get-ObjectPropertyValue $step 'inconclusive_on_stdout_regex'))
      inconclusive_on_stderr_regex = @(ConvertTo-StringArray (Get-ObjectPropertyValue $step 'inconclusive_on_stderr_regex'))
      inconclusive_on_output_regex = @(ConvertTo-StringArray (Get-ObjectPropertyValue $step 'inconclusive_on_output_regex'))
      match_rules = @($rules)
      artifact_files = @((Get-ObjectPropertyValue $step 'artifact_files'))
      structured_results = @((Get-ObjectPropertyValue $step 'structured_results'))
    }
  }
  return @($normalized)
}

function Get-PipelineRuleSignal($Step, [string]$StdoutText, [string]$StderrText) {
  $combinedText = ([string]$StdoutText) + [char]10 + ([string]$StderrText)
  foreach ($rule in @(Get-ObjectPropertyValue $Step 'match_rules')) {
    $regex = [string](Get-ObjectPropertyValue $rule 'regex')
    if ([string]::IsNullOrWhiteSpace($regex)) { continue }
    $targetName = [string](Get-ObjectPropertyValue $rule 'target')
    if ([string]::IsNullOrWhiteSpace($targetName)) { $targetName = 'output' }
    $targetName = $targetName.ToLowerInvariant()
    $target = switch ($targetName) {
      'stdout' { [string]$StdoutText }
      'stderr' { [string]$StderrText }
      default { $combinedText }
    }
    $matched = $false
    try { $matched = [regex]::IsMatch($target, $regex) } catch { throw "invalid native pipeline regex '$regex': $($_.Exception.Message)" }
    if ($matched) {
      $reasonText = [string](Get-ObjectPropertyValue $rule 'reason')
      if ([string]::IsNullOrWhiteSpace($reasonText)) { $reasonText = "pipeline rule matched: $regex" }
      $status = [string](Get-ObjectPropertyValue $rule 'status')
      if ([string]::IsNullOrWhiteSpace($status)) { $status = 'INCONCLUSIVE' }
      return [ordered]@{ status = $status.ToUpperInvariant(); reason = $reasonText; metrics = [ordered]@{ matched_regex = $regex; matched_target = $targetName } }
    }
  }
  return [ordered]@{ status = 'PASS'; reason = ''; metrics = [ordered]@{} }
}

function Invoke-NativePipeline([array]$Steps, [string]$WorkDir, [string]$ArtifactDir) {
  $stepResults = @()
  $failedStep = $null
  foreach ($step in @($Steps)) {
    $stepLogDir = Join-Path $ArtifactDir ('pipeline_step_{0:00}_{1}' -f [int]$step.index, (ConvertTo-SafeName $step.name))
    New-Item -ItemType Directory -Path $stepLogDir -Force | Out-Null
    $artifactSpecs = @((Get-ObjectPropertyValue $step 'artifact_files'))
    $structuredResultSpecs = @((Get-ObjectPropertyValue $step 'structured_results'))
    $beforeConsistency = Get-ConsistencySnapshot $WorkDir $artifactSpecs
    $stepArgs = @((Get-ObjectPropertyValue $step 'args'))
    if ($stepArgs.Count -gt 0) {
      $result = Invoke-ProcessCommand ([string]$step.command) @($stepArgs | ForEach-Object { [string]$_ }) $WorkDir ([int]$step.timeout_seconds)
    } else {
      $result = Invoke-ProcessCommand (Resolve-PowerShellHost) @('-NoProfile', '-Command', [string]$step.command) $WorkDir ([int]$step.timeout_seconds)
    }
    $afterConsistency = Get-ConsistencySnapshot $WorkDir $artifactSpecs
    $workspaceDelta = Get-WorkspaceDelta $beforeConsistency.source $afterConsistency.source
    $structuredResults = @(Get-StructuredResultReferences $WorkDir ([string]$result.stdout) ([string]$result.stderr) $structuredResultSpecs $stepLogDir)
    $consistency = [ordered]@{
      before = $beforeConsistency
      after = $afterConsistency
      workspace_delta = $workspaceDelta
      structured_results = $structuredResults
    }
    Set-Content -LiteralPath (Join-Path $stepLogDir 'stdout.log') -Value ([string]$result.stdout) -Encoding UTF8
    Set-Content -LiteralPath (Join-Path $stepLogDir 'stderr.log') -Value ([string]$result.stderr) -Encoding UTF8
    Write-StatusJson (Join-Path $stepLogDir 'consistency.json') $consistency
    $ruleSignal = Get-PipelineRuleSignal $step ([string]$result.stdout) ([string]$result.stderr)
    $structuredSignal = Get-StructuredResultSignal $structuredResults
    $semanticSignal = if ([string]$ruleSignal.status -ne 'PASS') { $ruleSignal } elseif ([string]$structuredSignal.status -ne 'PASS') { $structuredSignal } else { [ordered]@{ status = 'PASS'; reason = ''; metrics = [ordered]@{} } }
    $status = if ($result.timed_out) { 'TIMEOUT' } elseif ($null -ne $result.error) { 'FAIL' } elseif ($result.exit_code -eq 0) { [string]$semanticSignal.status } else { 'FAIL' }
    $reasonText = if ($status -ne 'PASS' -and -not [string]::IsNullOrWhiteSpace([string]$semanticSignal.reason)) { [string]$semanticSignal.reason } elseif ($status -eq 'TIMEOUT') { "step timed out after $($step.timeout_seconds)s" } elseif ($status -eq 'FAIL') { "step exited with code $($result.exit_code)" } else { '' }
    $record = [ordered]@{
      index = [int]$step.index
      name = [string]$step.name
      command = [string]$step.command
      args = @($stepArgs)
      timeout_seconds = [int]$step.timeout_seconds
      status = $status
      reason = $reasonText
      exit_code = $result.exit_code
      timed_out = $result.timed_out
      error = $result.error
      rule_metrics = $ruleSignal.metrics
      structured_result_signal = $structuredSignal
      duration_ms = $result.duration_ms
      stdout_file = (Join-Path $stepLogDir 'stdout.log')
      stderr_file = (Join-Path $stepLogDir 'stderr.log')
      consistency_file = (Join-Path $stepLogDir 'consistency.json')
      consistency = $consistency
      stdout_excerpt = (Limit-LogText ([string]$result.stdout) 80)
      stderr_excerpt = (Limit-LogText ([string]$result.stderr) 80)
    }
    $stepResults += $record
    if ($status -ne 'PASS') {
      $failedStep = $record
      break
    }
  }

  $overall = if ($null -eq $failedStep) { 'PASS' } elseif ($failedStep.status -eq 'TIMEOUT') { 'TIMEOUT' } elseif ($failedStep.status -eq 'INCONCLUSIVE') { 'INCONCLUSIVE' } else { 'FAIL' }
  $pipelineResult = [ordered]@{
    status = $overall
    failed_step_index = if ($null -eq $failedStep) { $null } else { $failedStep.index }
    failed_step_name = if ($null -eq $failedStep) { '' } else { $failedStep.name }
    steps = @($stepResults)
  }
  $policy = Get-PipelinePolicyReport $pipelineResult
  $pipelineResult.policy = $policy
  Write-StatusJson (Join-Path $ArtifactDir 'pipeline_steps.json') $pipelineResult
  Write-StatusJson (Join-Path $ArtifactDir 'pipeline_policy.json') $policy
  return $pipelineResult
}

function Get-PipelineResultText($PipelineResult) {
  $lines = @()
  foreach ($step in @($PipelineResult.steps)) {
    $suffix = if (-not [string]::IsNullOrWhiteSpace([string]$step.reason)) { " reason=$($step.reason)" } else { '' }
    $lines += "- Step $($step.index) ($($step.name)): $($step.status) exit=$($step.exit_code) timeout=$($step.timed_out) duration_ms=$($step.duration_ms)$suffix"
  }
  $policy = Get-ObjectPropertyValue $PipelineResult 'policy'
  if ($null -ne $policy) {
    $lines += "- Policy: $([string](Get-ObjectPropertyValue $policy 'status')) continue_ok=$([bool](Get-ObjectPropertyValue $policy 'continue_ok'))"
    foreach ($issue in @((Get-ObjectPropertyValue $policy 'issues'))) {
      $lines += "  - $([string](Get-ObjectPropertyValue $issue 'code')) step=$([string](Get-ObjectPropertyValue $issue 'step')) $([string](Get-ObjectPropertyValue $issue 'reason'))"
    }
  }
  return ($lines -join [char]10)
}

function Write-PipelineArtifacts($PipelineResult, [string]$SummaryPath, [string]$DiagnosisPath, [string]$EvidencePath) {
  $failed = $null
  foreach ($step in @($PipelineResult.steps)) {
    if ($step.status -ne 'PASS') { $failed = $step; break }
  }
  $summary = if ($PipelineResult.status -eq 'PASS') {
    "All native pipeline steps completed successfully.`n`n$(Get-PipelineResultText $PipelineResult)"
  } else {
    "Native pipeline stopped at step $($PipelineResult.failed_step_index) ($($PipelineResult.failed_step_name)).`n`n$(Get-PipelineResultText $PipelineResult)"
  }
  Set-Content -LiteralPath $SummaryPath -Value $summary -Encoding UTF8

  if ($null -eq $failed) {
    Set-Content -LiteralPath $DiagnosisPath -Value '{}' -Encoding UTF8
    Set-Content -LiteralPath $EvidencePath -Value '' -Encoding UTF8
    return $false
  }

  $diagnosis = [ordered]@{
    status = [string]$PipelineResult.status
    phase = 'native_pipeline'
    confidence = 'high'
    root_cause_hypothesis = if ([string]::IsNullOrWhiteSpace([string]$failed.reason)) { "Pipeline step '$($failed.name)' returned $($failed.status)." } else { [string]$failed.reason }
    not_root_causes = @()
    key_markers = @("step=$($failed.index)", "status=$($failed.status)", "exit_code=$($failed.exit_code)")
    metrics = [ordered]@{ duration_ms = $failed.duration_ms; rule_metrics = $failed.rule_metrics; structured_result_signal = $failed.structured_result_signal; policy = (Get-ObjectPropertyValue $PipelineResult 'policy') }
    next_action = 'Read pipeline_steps.json and the step stdout/stderr excerpts; rerun a focused executor diagnosis only if this structured evidence is insufficient.'
  }
  Write-StatusJson $DiagnosisPath $diagnosis
  $evidenceLines = @(
    "step=$($failed.index) name=$($failed.name) status=$($failed.status)",
    "reason=$($failed.reason)",
    "stdout_file=$($failed.stdout_file)",
    "stderr_file=$($failed.stderr_file)",
    'stdout_excerpt:',
    ([string]$failed.stdout_excerpt)
  )
  if (-not [string]::IsNullOrWhiteSpace([string]$failed.stderr_excerpt)) {
    $evidenceLines += 'stderr_excerpt:'
    $evidenceLines += ([string]$failed.stderr_excerpt)
  }
  Set-Content -LiteralPath $EvidencePath -Value (($evidenceLines | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) -join [char]10) -Encoding UTF8
  return $true
}

function Get-JsonObjectText([string]$Text) {
  if ([string]::IsNullOrWhiteSpace($Text)) { return '' }
  $trimmed = $Text.Trim()
  if ($trimmed.StartsWith('{') -and $trimmed.EndsWith('}')) { return $trimmed }
  $fenced = [regex]::Match($Text, '(?is)```\s*json\s*(.*?)```')
  if ($fenced.Success) { return $fenced.Groups[1].Value.Trim() }
  $start = $Text.IndexOf('{')
  $end = $Text.LastIndexOf('}')
  if ($start -ge 0 -and $end -gt $start) { return $Text.Substring($start, $end - $start + 1) }
  return ''
}

function Get-ClaudeResultText([string]$Text) {
  if ([string]::IsNullOrWhiteSpace($Text)) { return '' }
  try {
    $json = $Text | ConvertFrom-Json
    $result = Get-ObjectPropertyValue $json 'result'
    if ($null -ne $result) { return [string]$result }
  } catch {}
  return $Text
}

function Get-ExecutorResponse([string]$ResultText) {
  $jsonText = Get-JsonObjectText $ResultText
  if (-not [string]::IsNullOrWhiteSpace($jsonText)) {
    try { return ($jsonText | ConvertFrom-Json) } catch {}
  }
  return $null
}

function Get-DefaultDiagnosis([string]$Status, [string]$ReasonText) {
  return [ordered]@{
    status = $Status
    phase = 'executor'
    confidence = 'low'
    root_cause_hypothesis = $ReasonText
    not_root_causes = @()
    key_markers = @($ReasonText)
    metrics = [ordered]@{}
    next_action = 'Rerun a focused executor diagnosis if this reason is insufficient.'
  }
}

function Write-ExecutorArtifacts($Response, [string]$ResultText, [string]$Status, [string]$ReasonText, [string]$SummaryPath, [string]$DiagnosisPath, [string]$EvidencePath) {
  $summaryValue = if ($null -ne $Response) { Get-ObjectPropertyValue $Response 'summary' } else { $null }
  $summaryLines = @()
  foreach ($item in @($summaryValue)) {
    if ($null -ne $item -and -not [string]::IsNullOrWhiteSpace([string]$item)) { $summaryLines += [string]$item }
  }
  if ($summaryLines.Count -eq 0 -and -not [string]::IsNullOrWhiteSpace($ResultText)) {
    $summaryLines = @($ResultText -split '\r?\n' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 80)
  }
  if ($summaryLines.Count -eq 0) { $summaryLines = @("STATUS: $Status", "REASON: $ReasonText") }
  Set-Content -LiteralPath $SummaryPath -Value ($summaryLines -join [char]10) -Encoding UTF8

  $diagnosis = if ($null -ne $Response) { Get-ObjectPropertyValue $Response 'diagnosis' } else { $null }
  if ($null -eq $diagnosis) { $diagnosis = Get-DefaultDiagnosis $Status $ReasonText }
  Write-StatusJson $DiagnosisPath $diagnosis

  $evidenceValue = if ($null -ne $Response) { Get-ObjectPropertyValue $Response 'evidence' } else { $null }
  $evidenceLines = @()
  foreach ($item in @($evidenceValue)) {
    if ($null -ne $item -and -not [string]::IsNullOrWhiteSpace([string]$item)) { $evidenceLines += [string]$item }
  }
  Set-Content -LiteralPath $EvidencePath -Value ($evidenceLines -join [char]10) -Encoding UTF8
}

function Test-TransientExecutorOutput([string]$StdoutText, [string]$StderrText) {
  $text = ([string]$StdoutText) + [char]10 + ([string]$StderrText)
  return ($text -match '(?i)\b(429|500|502|503|504)\b|rate.?limit|ECONNRESET|ETIMEDOUT|network|temporarily unavailable')
}

function Invoke-ClaudeExecutor([string]$PromptPath, [string]$ProjectRoot, [string]$StdoutPath, [string]$StderrPath, [int]$TimeoutSec, [int]$MaxAttempts, [string]$ArgsPath) {
  $claudePs1 = Resolve-ClaudePs1
  $claudeArgs = @('-p', '--output-format', 'json', '--permission-mode', 'bypassPermissions', '--tools', 'Bash,Read,Glob,Grep', '--disallowedTools', 'Edit,Write,MultiEdit,NotebookEdit', '--no-chrome', '--max-budget-usd', ([string]$MaxBudgetUsd))
  if (-not [string]::IsNullOrWhiteSpace($Model)) { $claudeArgs += @('--model', $Model) }
  $claudeArgs | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $ArgsPath -Encoding UTF8

  $runner = @'
$ErrorActionPreference = 'Stop'
$claude = $env:CLAUDE_EXECUTOR_CLAUDE_PS1
$argsPath = $env:CLAUDE_EXECUTOR_ARGS_JSON
$promptPath = $env:CLAUDE_EXECUTOR_PROMPT_FILE
$claudeArgs = @(Get-Content -LiteralPath $argsPath -Raw | ConvertFrom-Json)
$prompt = Get-Content -LiteralPath $promptPath -Raw
& $claude @claudeArgs $prompt
exit $LASTEXITCODE
'@
  $runnerPath = Join-Path (Split-Path -Parent $PromptPath) 'run_claude_executor.ps1'
  Set-Content -LiteralPath $runnerPath -Value $runner -Encoding UTF8

  $attempt = 0
  while ($true) {
    $attempt++
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo.FileName = (Resolve-PowerShellHost)
    $process.StartInfo.Arguments = Join-Arguments @('-NoProfile', '-File', $runnerPath)
    $process.StartInfo.WorkingDirectory = $ProjectRoot
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.RedirectStandardInput = $false
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.EnvironmentVariables['CLAUDE_EXECUTOR_CLAUDE_PS1'] = $claudePs1
    $process.StartInfo.EnvironmentVariables['CLAUDE_EXECUTOR_ARGS_JSON'] = $ArgsPath
    $process.StartInfo.EnvironmentVariables['CLAUDE_EXECUTOR_PROMPT_FILE'] = $PromptPath
    $process.StartInfo.EnvironmentVariables['PYTHONIOENCODING'] = 'utf-8'
    $process.StartInfo.EnvironmentVariables['LANG'] = 'C.UTF-8'
    $process.StartInfo.EnvironmentVariables['LC_ALL'] = 'C.UTF-8'
    Set-ProcessEncodingIfAvailable $process.StartInfo 'StandardOutputEncoding' $utf8NoBom
    Set-ProcessEncodingIfAvailable $process.StartInfo 'StandardErrorEncoding' $utf8NoBom

    $timedOut = $false
    $exitCode = $null
    $errorMessage = $null
    $stdoutText = ''
    $stderrText = ''
    try {
      [void]$process.Start()
      $stdoutTask = $process.StandardOutput.ReadToEndAsync()
      $stderrTask = $process.StandardError.ReadToEndAsync()
      if (-not $process.WaitForExit([Math]::Max(1, $TimeoutSec) * 1000)) {
        $timedOut = $true
        try { Stop-ProcessTree $process } catch { $errorMessage = $_.Exception.Message }
        [void]$process.WaitForExit(5000)
      } else {
        $process.WaitForExit()
      }
      try { $exitCode = $process.ExitCode } catch { $exitCode = $null }
      $stdoutText = Get-TaskResultOrDefault $stdoutTask 5000
      $stderrText = Get-TaskResultOrDefault $stderrTask 5000
    } catch {
      $errorMessage = $_.Exception.Message
    } finally {
      Set-Content -LiteralPath $StdoutPath -Value $stdoutText -Encoding UTF8
      Set-Content -LiteralPath $StderrPath -Value $stderrText -Encoding UTF8
    }

    if ($attempt -gt $MaxAttempts -or $timedOut -or -not (Test-TransientExecutorOutput $stdoutText $stderrText)) {
      return [ordered]@{ timed_out = $timedOut; exit_code = $exitCode; error = $errorMessage; stdout = $stdoutText; stderr = $stderrText; attempts = $attempt; retried = ($attempt -gt 1) }
    }
    Start-Sleep -Seconds ([Math]::Min(30, 10 * $attempt))
  }
}

function New-ExecutorPrompt([string]$TaskText, [string]$ExecutionRoot) {
  $bashRoot = ConvertTo-BashPath $ExecutionRoot
  return @"
You are a read-only executor for this repository. Codex owns code edits, architecture judgment, and final decisions.
You may run commands, read files, capture logs, and summarize evidence. Do not edit source, docs, config, or git state.

Execution root: $ExecutionRoot
Bash execution root: $bashRoot
Resolve relative paths from the execution root. Start Bash commands with: cd "$bashRoot" && <command>.

Return exactly one JSON object, no Markdown:
{
  "status": "PASS|FAIL|TIMEOUT|INCONCLUSIVE",
  "reason": "one sentence",
  "summary": ["up to 8 concise bullets"],
  "diagnosis": {
    "status": "PASS|FAIL|TIMEOUT|INCONCLUSIVE",
    "phase": "preflight|command_start|execution|test|build|artifact|postprocess|unknown",
    "confidence": "high|medium|low",
    "root_cause_hypothesis": "self-contained cause or observation",
    "not_root_causes": [],
    "key_markers": [],
    "metrics": {},
    "next_action": "focused next action"
  },
  "evidence": ["3-30 short evidence lines for non-PASS; empty for low-risk PASS"]
}

If you cannot determine the cause, return INCONCLUSIVE with the best available evidence and a focused next diagnostic command.

Task:
$TaskText
"@
}

function Test-ExpectedResourceLocksHeld([string]$LockRoot, [string[]]$Resources, [string]$OwnerId) {
  $normalized = @(Get-NormalizedResourceList $Resources)
  if ($normalized.Count -eq 0) { return $true }
  $entries = @(Get-ResourceLockEntries $LockRoot)
  foreach ($resourceName in $normalized) {
    $match = $null
    foreach ($entry in $entries) {
      if ([string]$entry.resource -eq $resourceName -and [string]$entry.id -eq $OwnerId) {
        $match = $entry
        break
      }
    }
    if ($null -eq $match) { return $false }
    try {
      if (-not (Test-ProcessAlive ([int]$match.process_id))) { return $false }
    } catch {
      return $false
    }
  }
  return $true
}

function Test-PipelineStepDirectoryExists([string]$ArtifactDir) {
  if (-not (Test-Path -LiteralPath $ArtifactDir -PathType Container)) { return $false }
  $stepDir = Get-ChildItem -LiteralPath $ArtifactDir -Directory -Filter 'pipeline_step_*' -ErrorAction SilentlyContinue | Select-Object -First 1
  return ($null -ne $stepDir)
}

function Wait-BackgroundStartupHealth {
  param(
    [System.Diagnostics.Process]$Process,
    [string]$StatusPath,
    [string]$DonePath,
    [string]$LockRoot,
    [string[]]$Resources,
    [string]$OwnerId,
    [string]$ArtifactDir,
    [bool]$RequirePipelineStep,
    [int]$TimeoutSeconds
  )
  $deadline = (Get-Date).AddSeconds([Math]::Max(1, $TimeoutSeconds))
  $lastState = ''
  $lastReason = ''
  while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $DonePath -PathType Leaf) {
      return [ordered]@{ ok = $true; state = 'done'; reason = 'background worker completed during startup health check'; process_id = $Process.Id }
    }
    $processAlive = Test-ProcessAlive ([int]$Process.Id)
    $state = ''
    try {
      if (Test-Path -LiteralPath $StatusPath -PathType Leaf) {
        $status = Get-Content -LiteralPath $StatusPath -Raw | ConvertFrom-Json
        $state = [string](Get-ObjectPropertyValue $status 'state')
      }
    } catch {}
    if (-not [string]::IsNullOrWhiteSpace($state)) { $lastState = $state }
    $locksOk = Test-ExpectedResourceLocksHeld $LockRoot $Resources $OwnerId
    $pipelineOk = (-not $RequirePipelineStep) -or (Test-PipelineStepDirectoryExists $ArtifactDir)
    if ($state -eq 'running' -and $processAlive -and $locksOk -and $pipelineOk) {
      return [ordered]@{ ok = $true; state = 'running'; reason = 'background worker reached running health gate'; process_id = $Process.Id }
    }
    if (-not $processAlive) {
      if (Test-Path -LiteralPath $DonePath -PathType Leaf) {
        return [ordered]@{ ok = $true; state = 'done'; reason = 'background worker exited after writing .done'; process_id = $Process.Id }
      }
      $lastReason = "background worker process exited before running; last_state=$lastState"
      break
    }
    $lastReason = "last_state=$lastState locks_ok=$locksOk pipeline_step_ok=$pipelineOk process_alive=$processAlive"
    Start-Sleep -Milliseconds 500
  }
  return [ordered]@{ ok = $false; state = if ([string]::IsNullOrWhiteSpace($lastState)) { 'unknown' } else { $lastState }; reason = "background worker did not pass startup health within ${TimeoutSeconds}s ($lastReason)"; process_id = $Process.Id }
}

function Get-CodexAction([string]$WorkerStatus, [string]$Override = '') {
  if (-not [string]::IsNullOrWhiteSpace($Override)) { return $Override }
  switch ($WorkerStatus) {
    'PASS' { return 'continue' }
    'TIMEOUT' { return 'increase_timeout' }
    'INCONCLUSIVE' { return 'read_diagnosis' }
    default { return 'read_diagnosis' }
  }
}

function Write-FinalStatus {
  param(
    [string]$State,
    [string]$WorkerStatus,
    [string]$WorkerReason,
    $ExitCode,
    [bool]$TimedOut,
    $ErrorMessage,
    [string]$CodexActionOverride = ''
  )
  $codexAction = Get-CodexAction $WorkerStatus $CodexActionOverride
  $status = [ordered]@{
    id = $script:JobId
    state = $State
    worker_status = $WorkerStatus
    worker_reason = $WorkerReason
    codex_action = $codexAction
    exit_code = $ExitCode
    timed_out = $TimedOut
    error = $ErrorMessage
    start_time = $script:StartTime
    end_time = (Get-Date -Format o)
    artifact_dir = $script:ArtifactDir
    task_file = $script:TaskCopyPath
    prompt_file = $script:PromptPath
    stdout_file = $script:StdoutPath
    stderr_file = $script:StderrPath
    result_file = $script:ResultPath
    summary_file = $script:SummaryPath
    diagnosis_file = $script:DiagnosisPath
    evidence_file = $script:EvidencePath
    metrics_file = $script:MetricsPath
    workspace_delta_file = $script:WorkspaceDeltaPath
    done_file = $script:DonePath
    timeout_seconds = $TimeoutSeconds
    resources = $script:ResourceList
    allow_workspace_changes = [bool]$AllowWorkspaceChanges
    no_worktree = [bool]$NoWorktree
    execution_root = $script:ExecutionRoot
    claude_project_root = $script:RepoRoot
    worktree = $script:WorktreeInfo
  }
  Write-StatusJson $script:StatusPath $status
  $done = [ordered]@{ status = $WorkerStatus; codex_action = $codexAction; reason = $WorkerReason; artifact = $script:ArtifactDir }
  Set-Content -LiteralPath $script:DonePath -Value ($done | ConvertTo-Json -Compress -Depth 8) -Encoding UTF8
  Write-ResultJson $done
}

function Invoke-BackgroundDispatch([array]$NativePipelineSteps) {
  $childArgs = @('-NoProfile', '-File', $PSCommandPath)
  if ($NativePipelineSteps.Count -gt 0) {
    $childArgs += @('-PipelineFile', $script:PipelineStepsPath)
  } else {
    $childArgs += @('-TaskFile', $script:TaskCopyPath)
  }
  $childArgs += @('-Name', $Name, '-TimeoutSeconds', ([string]$TimeoutSeconds), '-JobId', $script:JobId, '-InternalBackgroundWorker', '-BackgroundStartupTimeoutSeconds', ([string]$BackgroundStartupTimeoutSeconds), '-MaxRetries', ([string]$MaxRetries), '-MaxBudgetUsd', ([string]$MaxBudgetUsd))
  if ($ForceForeground) { $childArgs += '-ForceForeground' }
  if (-not [string]::IsNullOrWhiteSpace($Reason)) { $childArgs += @('-Reason', $Reason) }
  if ($script:ResourceList.Count -gt 0) { $childArgs += @('-Resource', ($script:ResourceList -join ',')) }
  if ($AllowWorkspaceChanges) { $childArgs += '-AllowWorkspaceChanges' }
  if ($NoWorktree) { $childArgs += '-NoWorktree' }
  if ($KeepWorktree) { $childArgs += '-KeepWorktree' }
  if (-not [string]::IsNullOrWhiteSpace($Model)) { $childArgs += @('-Model', $Model) }

  Write-StatusJson $script:StatusPath ([ordered]@{
    id = $script:JobId
    state = 'starting'
    background = $true
    resources = $script:ResourceList
    artifact_dir = $script:ArtifactDir
    timeout_seconds = $TimeoutSeconds
    health_check_seconds = $BackgroundStartupTimeoutSeconds
  })
  Write-Output "started: $script:JobId"
  Write-Output "artifact_dir: $script:ArtifactDir"

  $childProcess = $null
  try {
    $childProcess = Start-Process -FilePath (Resolve-PowerShellHost) -ArgumentList (Join-Arguments $childArgs) -WorkingDirectory $script:RepoRoot -WindowStyle Hidden -RedirectStandardOutput $script:BackgroundChildStdoutPath -RedirectStandardError $script:BackgroundChildStderrPath -PassThru
  } catch {
    $reasonText = "background worker failed to start: $($_.Exception.Message)"
    Write-FinalStatus 'failed' 'FAIL' $reasonText $null $false $_.Exception.Message 'retry_executor'
    exit 1
  }

  $health = Wait-BackgroundStartupHealth -Process $childProcess -StatusPath $script:StatusPath -DonePath $script:DonePath -LockRoot $script:LockRoot -Resources $script:ResourceList -OwnerId $script:JobId -ArtifactDir $script:ArtifactDir -RequirePipelineStep ($NativePipelineSteps.Count -gt 0) -TimeoutSeconds $BackgroundStartupTimeoutSeconds
  Write-StatusJson $script:BackgroundHealthPath $health
  if (-not [bool]$health.ok) {
    try { Stop-ProcessTree $childProcess } catch {}
    Release-ResourceLocksByOwner $script:LockRoot $script:ResourceList $script:JobId
    $reasonText = [string]$health.reason
    Set-Content -LiteralPath $script:SummaryPath -Value "STATUS: FAIL`nREASON: $reasonText" -Encoding UTF8
    Write-StatusJson $script:DiagnosisPath (Get-DefaultDiagnosis 'FAIL' $reasonText)
    $childStdout = if (Test-Path -LiteralPath $script:BackgroundChildStdoutPath -PathType Leaf) { Limit-LogText (Get-Content -LiteralPath $script:BackgroundChildStdoutPath -Raw) 40 } else { '' }
    $childStderr = if (Test-Path -LiteralPath $script:BackgroundChildStderrPath -PathType Leaf) { Limit-LogText (Get-Content -LiteralPath $script:BackgroundChildStderrPath -Raw) 40 } else { '' }
    Set-Content -LiteralPath $script:EvidencePath -Value (@($reasonText, 'child_stdout:', $childStdout, 'child_stderr:', $childStderr) -join [char]10) -Encoding UTF8
    Write-FinalStatus 'failed' 'FAIL' $reasonText $null $false $null 'retry_executor'
    exit 1
  }

  $started = [ordered]@{ status = 'STARTED'; codex_action = 'poll_done'; reason = [string]$health.reason; artifact = $script:ArtifactDir }
  Write-ResultJson $started
  exit 0
}

function Cleanup-CurrentRun {
  if ($script:CleanupDone) { return }
  $script:CleanupDone = $true
  if ($script:LockPaths.Count -gt 0) {
    Release-ResourceLocks $script:LockPaths $script:JobId
  }
  if (-not $script:KeepWorktree -and -not [string]::IsNullOrWhiteSpace($script:WorktreePath)) {
    Remove-ExecutorWorktree $script:RepoRoot $script:WorktreePath $false
  }
}

function Main {
  $script:RepoRoot = Get-RepoRoot
  $script:ArtifactRoot = Join-Path $script:RepoRoot '.cache\claude_executor'
  $script:LockRoot = Join-Path $script:RepoRoot '.cache\resource_locks'
  New-Item -ItemType Directory -Path $script:ArtifactRoot -Force | Out-Null
  New-Item -ItemType Directory -Path $script:LockRoot -Force | Out-Null

  if ($ListLocks) {
    Get-ResourceLockEntries $script:LockRoot | ConvertTo-Json -Depth 8
    return
  }
  if ($ClearStaleLocks) {
    foreach ($entry in @(Get-ResourceLockEntries $script:LockRoot)) {
      if ($null -eq $entry.process_id -or -not (Test-ProcessAlive ([int]$entry.process_id))) {
        Remove-Item -LiteralPath $entry.path -Force -ErrorAction SilentlyContinue
      }
    }
    Get-ResourceLockEntries $script:LockRoot | ConvertTo-Json -Depth 8
    return
  }

  $script:JobId = if (-not [string]::IsNullOrWhiteSpace($JobId)) {
    ConvertTo-SafeName $JobId
  } elseif ($InternalBackgroundWorker -and $PSCmdlet.ParameterSetName -eq 'TaskFile') {
    $resolvedTask = Resolve-Path -LiteralPath $TaskFile -ErrorAction SilentlyContinue
    $leaf = if ($null -ne $resolvedTask) { Split-Path -Leaf (Split-Path -Parent $resolvedTask.Path) } else { '' }
    if ([string]::IsNullOrWhiteSpace($leaf)) { New-JobId $Name } else { $leaf }
  } elseif ($InternalBackgroundWorker -and $PSCmdlet.ParameterSetName -eq 'PipelineFile') {
    $resolvedPipeline = Resolve-Path -LiteralPath $PipelineFile -ErrorAction SilentlyContinue
    $leaf = if ($null -ne $resolvedPipeline) { Split-Path -Leaf (Split-Path -Parent $resolvedPipeline.Path) } else { '' }
    if ([string]::IsNullOrWhiteSpace($leaf)) { New-JobId $Name } else { $leaf }
  } else {
    New-JobId $Name
  }

  $script:ArtifactDir = Join-Path $script:ArtifactRoot $script:JobId
  New-Item -ItemType Directory -Path $script:ArtifactDir -Force | Out-Null
  Set-Content -LiteralPath (Join-Path $script:ArtifactRoot 'latest.path') -Value $script:ArtifactDir -Encoding UTF8

  $script:TaskCopyPath = Join-Path $script:ArtifactDir 'task.md'
  $script:PromptPath = Join-Path $script:ArtifactDir 'prompt.md'
  $script:StatusPath = Join-Path $script:ArtifactDir 'status.json'
  $script:ResultPath = Join-Path $script:ArtifactDir 'result.md'
  $script:SummaryPath = Join-Path $script:ArtifactDir 'summary.md'
  $script:DiagnosisPath = Join-Path $script:ArtifactDir 'diagnosis.json'
  $script:EvidencePath = Join-Path $script:ArtifactDir 'evidence.md'
    $script:MetricsPath = Join-Path $script:ArtifactDir 'metrics.json'
    $script:WorkspaceDeltaPath = Join-Path $script:ArtifactDir 'workspace_delta.json'
    $script:StdoutPath = Join-Path $script:ArtifactDir 'claude_stdout.json'
    $script:StderrPath = Join-Path $script:ArtifactDir 'claude_stderr.log'
    $script:DonePath = Join-Path $script:ArtifactDir '.done'
  $script:PipelineStepsPath = Join-Path $script:ArtifactDir 'pipeline_steps_input.json'
  $script:ClaudeArgsPath = Join-Path $script:ArtifactDir 'claude_args.json'
  $script:BackgroundHealthPath = Join-Path $script:ArtifactDir 'background_health.json'
  $script:BackgroundChildStdoutPath = Join-Path $script:ArtifactDir 'background_child_stdout.log'
  $script:BackgroundChildStderrPath = Join-Path $script:ArtifactDir 'background_child_stderr.log'
  $script:StartTime = Get-Date -Format o
  $script:ResourceList = @(Get-NormalizedResourceList $Resource)
  $script:ExecutionRoot = $script:RepoRoot
  $script:WorktreeInfo = [ordered]@{ enabled = $false; path = ''; head = ''; root = ''; snapshot = $null; keep = [bool]$KeepWorktree }
  $script:WorkspaceBefore = Get-GitWorkspaceSnapshot $script:RepoRoot

  $taskText = Get-TaskText
  Set-Content -LiteralPath $script:TaskCopyPath -Value $taskText -Encoding UTF8
  $nativePipelineSteps = @()
  if ($PSCmdlet.ParameterSetName -in @('PipelineFile', 'PipelineJson')) {
    $nativePipelineSteps = @(Get-NativePipelineSteps)
    $nativePipelineSteps | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $script:PipelineStepsPath -Encoding UTF8
  } else {
    Set-Content -LiteralPath $script:PipelineStepsPath -Value '[]' -Encoding UTF8
  }

  if ((-not $Background) -and (-not $InternalBackgroundWorker) -and $TimeoutSeconds -gt 30 -and (-not $ForceForeground)) {
    $reasonText = "Long foreground executor task refused: timeout_seconds=$TimeoutSeconds exceeds 30. Use -Background or -ForceForeground."
    Set-Content -LiteralPath $script:SummaryPath -Value "STATUS: FAIL`nREASON: $reasonText" -Encoding UTF8
    Write-StatusJson $script:DiagnosisPath (Get-DefaultDiagnosis 'FAIL' $reasonText)
    Set-Content -LiteralPath $script:EvidencePath -Value $reasonText -Encoding UTF8
    Write-FinalStatus 'rejected' 'FAIL' $reasonText $null $false $null 'use_background'
    exit 4
  }

  if ($Background) {
    Invoke-BackgroundDispatch $nativePipelineSteps
    return
  }

  $script:LockPaths = @(Acquire-ResourceLocks $script:ResourceList $script:LockRoot $script:JobId $script:ArtifactDir)
  $useWorktree = (-not $NoWorktree) -and (-not $AllowWorkspaceChanges) -and ($script:ResourceList.Count -eq 0)
  if ($useWorktree) {
    $worktree = New-ExecutorWorktree $script:RepoRoot $script:JobId
    $script:WorktreePath = [string]$worktree.path
    $script:ExecutionRoot = [string]$worktree.path
    $script:WorktreeInfo = $worktree
    $script:WorktreeInfo.keep = [bool]$KeepWorktree
  }

  Write-StatusJson $script:StatusPath ([ordered]@{
    id = $script:JobId
    state = 'running'
    start_time = $script:StartTime
    artifact_dir = $script:ArtifactDir
    timeout_seconds = $TimeoutSeconds
    resources = $script:ResourceList
    execution_root = $script:ExecutionRoot
    claude_project_root = $script:RepoRoot
    worktree = $script:WorktreeInfo
  })

  if ($nativePipelineSteps.Count -gt 0) {
    $pipelineResult = Invoke-NativePipeline $nativePipelineSteps $script:ExecutionRoot $script:ArtifactDir
    $diagnosisValid = Write-PipelineArtifacts $pipelineResult $script:SummaryPath $script:DiagnosisPath $script:EvidencePath
    $resultText = "STATUS: $($pipelineResult.status)`nREASON: $(if ($pipelineResult.status -eq 'PASS') { 'All native pipeline steps completed successfully.' } else { "Native pipeline stopped at step $($pipelineResult.failed_step_index) ($($pipelineResult.failed_step_name))." })`n`nSUMMARY:`n$(Get-PipelineResultText $pipelineResult)"
    Set-Content -LiteralPath $script:ResultPath -Value $resultText -Encoding UTF8
    Write-StatusJson $script:MetricsPath ([ordered]@{ diagnosis_valid = $diagnosisValid; pipeline = $pipelineResult })
    $workerStatus = [string]$pipelineResult.status
    $workerReason = if ($workerStatus -eq 'PASS') { 'All native pipeline steps completed successfully.' } else { "Native pipeline stopped at step $($pipelineResult.failed_step_index) ($($pipelineResult.failed_step_name))." }
    $exitCode = if ($workerStatus -eq 'PASS') { 0 } elseif ($workerStatus -eq 'TIMEOUT') { 124 } else { 1 }
    $state = if ($workerStatus -eq 'PASS') { 'completed' } elseif ($workerStatus -eq 'TIMEOUT') { 'timeout' } else { 'failed' }
    Write-FinalStatus $state $workerStatus $workerReason $exitCode ($workerStatus -eq 'TIMEOUT') $null
    exit $(if ($workerStatus -eq 'PASS') { 0 } elseif ($workerStatus -eq 'INCONCLUSIVE') { 3 } else { 2 })
  }

  $prompt = New-ExecutorPrompt $taskText $script:ExecutionRoot
  Set-Content -LiteralPath $script:PromptPath -Value $prompt -Encoding UTF8
  $run = Invoke-ClaudeExecutor $script:PromptPath $script:RepoRoot $script:StdoutPath $script:StderrPath $TimeoutSeconds ($MaxRetries + 1) $script:ClaudeArgsPath
  $resultText = Get-ClaudeResultText ([string]$run.stdout)
  Set-Content -LiteralPath $script:ResultPath -Value $resultText -Encoding UTF8
  $response = Get-ExecutorResponse $resultText

  if ([bool]$run.timed_out) {
    $workerStatus = 'TIMEOUT'
    $workerReason = "executor timeout after ${TimeoutSeconds}s"
  } elseif ($null -ne $run.error) {
    $workerStatus = 'FAIL'
    $workerReason = [string]$run.error
  } elseif (Test-TransientExecutorOutput ([string]$run.stdout) ([string]$run.stderr)) {
    $workerStatus = 'INCONCLUSIVE'
    $workerReason = 'transient executor API/network error; retry executor'
  } elseif ($null -ne $response -and -not [string]::IsNullOrWhiteSpace([string](Get-ObjectPropertyValue $response 'status'))) {
    $workerStatus = ([string](Get-ObjectPropertyValue $response 'status')).ToUpperInvariant()
    $workerReason = [string](Get-ObjectPropertyValue $response 'reason')
    if ([string]::IsNullOrWhiteSpace($workerReason)) { $workerReason = "executor returned $workerStatus" }
  } elseif ($null -ne $run.exit_code -and $run.exit_code -ne 0) {
    $workerStatus = 'FAIL'
    $workerReason = "Claude executor exited with code $($run.exit_code)"
  } else {
    $workerStatus = 'INCONCLUSIVE'
    $workerReason = 'Claude executor did not return the required JSON object'
  }

  Write-ExecutorArtifacts $response $resultText $workerStatus $workerReason $script:SummaryPath $script:DiagnosisPath $script:EvidencePath
  $workspaceAfter = Get-GitWorkspaceSnapshot $script:RepoRoot
  $workspaceDelta = Get-WorkspaceDelta $script:WorkspaceBefore $workspaceAfter
  Write-StatusJson $script:WorkspaceDeltaPath $workspaceDelta
  Write-StatusJson $script:MetricsPath ([ordered]@{ attempts = $run.attempts; retried = $run.retried; stdout_bytes = ([string]$run.stdout).Length; stderr_bytes = ([string]$run.stderr).Length; workspace_delta = $workspaceDelta })
  $state = if ($workerStatus -eq 'PASS') { 'completed' } elseif ($workerStatus -eq 'TIMEOUT') { 'timeout' } else { 'failed' }
  $actionOverride = if ($workerReason -match '(?i)transient.*retry') { 'retry_executor' } else { '' }
  Write-FinalStatus $state $workerStatus $workerReason $run.exit_code ([bool]$run.timed_out) $run.error $actionOverride
  exit $(if ($workerStatus -eq 'PASS') { 0 } elseif ($workerStatus -eq 'INCONCLUSIVE') { 3 } else { 2 })
}

try {
  Main
} catch {
  $message = $_.Exception.Message
  try {
    if ($null -ne $script:ArtifactDir -and -not [string]::IsNullOrWhiteSpace($script:ArtifactDir)) {
      Set-Content -LiteralPath $script:SummaryPath -Value "STATUS: FAIL`nREASON: $message" -Encoding UTF8
      Write-StatusJson $script:DiagnosisPath (Get-DefaultDiagnosis 'FAIL' $message)
      Set-Content -LiteralPath $script:EvidencePath -Value $message -Encoding UTF8
      Set-Content -LiteralPath $script:ResultPath -Value "STATUS: FAIL`nREASON: $message" -Encoding UTF8
      Write-FinalStatus 'failed' 'FAIL' $message $null $false $message
    } else {
      Write-Error $message
    }
  } catch {
    Write-Error $message
  }
  exit 1
} finally {
  Cleanup-CurrentRun
}
