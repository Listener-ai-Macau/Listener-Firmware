# Shared helpers for AI collaboration workflow scripts.

function Resolve-AiIdentity {
    param(
        [string]$ExplicitIdentity,
        [string]$ParameterName = "Assignee"
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitIdentity)) {
        return $ExplicitIdentity
    }

    foreach ($name in @("AI_AGENT_ID", "TAI_AGENT_ID")) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if (-not [string]::IsNullOrWhiteSpace($value)) {
            return $value
        }
    }

    throw "AI identity is not set. Set AI_AGENT_ID/TAI_AGENT_ID or pass -$ParameterName explicitly."
}

function Get-StatusFileForPlan {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Plan,
        [Parameter(Mandatory=$true)]
        [string]$PlansDir
    )

    $candidate = Join-Path $PlansDir "${Plan}_status.json"
    if (Test-Path $candidate) {
        return $candidate
    }

    $indexFile = Join-Path $PlansDir "task_index.json"
    if (Test-Path $indexFile) {
        $index = Get-Content $indexFile -Raw | ConvertFrom-Json
        $mappedTask = $index.tasks | Where-Object {
            $_.task_slug -eq $Plan -or $_.legacy_id -eq $Plan
        } | Select-Object -First 1
        if ($mappedTask -and $mappedTask.status_file) {
            $mappedFile = Join-Path $PlansDir $mappedTask.status_file
            if (Test-Path $mappedFile) {
                return $mappedFile
            }
        }
    }

    throw "Status file not found for plan '$Plan'."
}

function Get-FirstLine {
    param([string[]]$Lines)

    if (-not $Lines) {
        return ""
    }

    return ([string]($Lines | Select-Object -First 1)).Trim()
}

function Test-StepDependenciesCompleted {
    param(
        [Parameter(Mandatory=$true)]
        $Step,
        [Parameter(Mandatory=$true)]
        [hashtable]$StepById
    )

    foreach ($dep in @($Step.depends_on)) {
        if (-not $StepById.ContainsKey([string]$dep)) {
            return $false
        }
        if ($StepById[[string]$dep].status -ne "completed") {
            return $false
        }
    }

    return $true
}

function Get-StepClaimState {
    param(
        [Parameter(Mandatory=$true)]
        $Step,
        [int]$StaleHours = 4
    )

    if ($Step.status -eq "pending" -and -not $Step.assignee) {
        return [PSCustomObject]@{
            Claimable = $true
            Stale = $false
            Basis = $null
            HoursOld = $null
        }
    }

    if ($Step.status -ne "in_progress" -or -not $Step.assignee) {
        return [PSCustomObject]@{
            Claimable = $false
            Stale = $false
            Basis = $null
            HoursOld = $null
        }
    }

    $basisValue = $null
    $basisName = $null
    if ($Step.PSObject.Properties["heartbeat_at"] -and $Step.heartbeat_at) {
        $basisValue = $Step.heartbeat_at
        $basisName = "heartbeat_at"
    } elseif ($Step.PSObject.Properties["claimed_at"] -and $Step.claimed_at) {
        $basisValue = $Step.claimed_at
        $basisName = "claimed_at"
    }

    if (-not $basisValue) {
        return [PSCustomObject]@{
            Claimable = $true
            Stale = $true
            Basis = "missing"
            HoursOld = $null
        }
    }

    try {
        $lastSeenAt = [DateTime]::Parse($basisValue)
        $hoursOld = ((Get-Date) - $lastSeenAt).TotalHours
        $isStale = $hoursOld -ge $StaleHours
        return [PSCustomObject]@{
            Claimable = $isStale
            Stale = $isStale
            Basis = $basisName
            HoursOld = $hoursOld
        }
    } catch {
        return [PSCustomObject]@{
            Claimable = $true
            Stale = $true
            Basis = $basisName
            HoursOld = $null
        }
    }
}

function Get-DerivedPlanStatus {
    param([Parameter(Mandatory=$true)]$PlanData)

    $steps = @()
    foreach ($phase in @($PlanData.phases)) {
        $steps += @($phase.steps)
    }

    if (-not $steps) {
        return "pending"
    }

    if (@($steps | Where-Object { $_.status -eq "in_progress" }).Count -gt 0) {
        return "in_progress"
    }

    if (@($steps | Where-Object { $_.status -eq "blocked" }).Count -gt 0) {
        return "blocked"
    }

    if (@($steps | Where-Object { $_.status -ne "completed" }).Count -eq 0) {
        return "completed"
    }

    return "pending"
}
