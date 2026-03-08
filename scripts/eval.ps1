param(
    [string]$Target = "data\blueprint.bin",
    [string]$Baselines = "",
    [string]$Abstraction = "",
    [ValidateSet("blueprint", "search", "runtime-pluribus")]
    [string]$Mode = "runtime-pluribus",
    [UInt64]$Hands = 20000,
    [int]$Seeds = 5,
    [UInt64]$SeedStart = 20260304,
    [UInt64]$SearchIters = 128,
    [int]$SearchDepth = 3,
    [int]$SearchThreads = 1,
    [ValidateSet("sample-final", "argmax")]
    [string]$SearchPick = "sample-final",
    [ValidateSet("inject", "translate")]
    [string]$OfftreeMode = "inject",
    [double]$MinBb100 = 0.0,
    [string]$Csv = "data\eval_report.csv",
    [string]$Json = "data\eval_report.json",
    [switch]$Build,
    [switch]$IgnoreAbstractionCompat,
    [string]$MainExe = "build\main.exe"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-Baselines {
    param([string]$Raw)

    $items = @()
    if ($null -ne $Raw -and $Raw.Trim().Length -gt 0) {
        $parts = $Raw -split "[;,]"
        foreach ($p in $parts) {
            $v = $p.Trim()
            if ($v.Length -gt 0) {
                $items += $v
            }
        }
    }
    return $items
}

function Get-Mean {
    param([double[]]$Values)
    if ($Values.Count -le 0) {
        return 0.0
    }
    $sum = 0.0
    foreach ($v in $Values) {
        $sum += [double]$v
    }
    return $sum / [double]$Values.Count
}

function Get-SampleStdDev {
    param([double[]]$Values)
    if ($Values.Count -le 1) {
        return 0.0
    }
    $m = Get-Mean -Values $Values
    $acc = 0.0
    foreach ($v in $Values) {
        $d = [double]$v - $m
        $acc += $d * $d
    }
    return [Math]::Sqrt($acc / [double]($Values.Count - 1))
}

function Ensure-Directory {
    param([string]$Path)
    $targetDir = [System.IO.Path]::GetDirectoryName($Path)
    if ([string]::IsNullOrWhiteSpace($targetDir)) {
        return
    }
    if (-not (Test-Path -LiteralPath $targetDir)) {
        New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
    }
}

function Csv-Escape {
    param([object]$Value)
    $s = ""
    if ($null -ne $Value) {
        $s = [string]$Value
    }
    $s = $s -replace '"', '""'
    return '"' + $s + '"'
}

function Csv-Num {
    param([object]$Value)
    if ($null -eq $Value) {
        return ""
    }
    if ($Value -is [double] -or $Value -is [single] -or $Value -is [decimal]) {
        return ([double]$Value).ToString("G17", [System.Globalization.CultureInfo]::InvariantCulture)
    }
    if ($Value -is [int] -or $Value -is [long] -or $Value -is [uint64] -or $Value -is [uint32]) {
        return ([string]$Value)
    }
    return [string]$Value
}

function Invoke-MatchRun {
    param(
        [string]$ExePath,
        [string]$TargetPath,
        [string]$BaselinePath,
        [string]$AbstractionPath,
        [string]$ModeName,
        [UInt64]$HandsCount,
        [UInt64]$SeedValue,
        [UInt64]$SearchItersValue,
        [int]$SearchDepthValue,
        [int]$SearchThreadsValue,
        [string]$SearchPickValue,
        [string]$OfftreeModeValue
        ,[switch]$IgnoreAbstractionCompatValue
    )

    $args = @("match", "--a", $TargetPath, "--b", $BaselinePath, "--hands", "$HandsCount")
    if ($AbstractionPath.Trim().Length -gt 0) {
        $args += @("--abstraction", $AbstractionPath)
    }
    if ($IgnoreAbstractionCompatValue) {
        $args += "--ignore-abstraction-compat"
    }
    switch ($ModeName) {
        "runtime-pluribus" { $args += @("--runtime-profile", "pluribus") }
        "search" { $args += @("--mode", "search") }
        default { $args += @("--mode", "blueprint") }
    }
    $args += @("--search-iters", "$SearchItersValue")
    $args += @("--search-depth", "$SearchDepthValue")
    $args += @("--search-threads", "$SearchThreadsValue")
    $args += @("--search-pick", $SearchPickValue)
    $args += @("--offtree-mode", $OfftreeModeValue)
    $args += @("--seed", "$SeedValue")

    Write-Host ("[eval] running: {0} {1}" -f $ExePath, ($args -join " "))
    $output = & $ExePath @args 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw ("match command failed (exit {0})`n{1}" -f $LASTEXITCODE, ($output -join [Environment]::NewLine))
    }

    $aLine = $null
    $bLine = $null
    foreach ($line in $output) {
        if ($line -match "^\s*A ev/chips_per_hand=") {
            $aLine = $line
        } elseif ($line -match "^\s*B ev/chips_per_hand=") {
            $bLine = $line
        }
    }

    if ($null -eq $aLine -or $null -eq $bLine) {
        throw ("failed to parse match output`n{0}" -f ($output -join [Environment]::NewLine))
    }

    $aMatch = [regex]::Match($aLine, "A ev/chips_per_hand=([\-0-9\.eE]+)\s+ev_bb/100=([\-0-9\.eE]+)")
    $bMatch = [regex]::Match($bLine, "B ev/chips_per_hand=([\-0-9\.eE]+)\s+ev_bb/100=([\-0-9\.eE]+)")
    if (-not $aMatch.Success -or -not $bMatch.Success) {
        throw ("failed to parse EV/bb100 values`nA: {0}`nB: {1}" -f $aLine, $bLine)
    }

    return [PSCustomObject]@{
        target_chips_per_hand = [double]$aMatch.Groups[1].Value
        target_bb100 = [double]$aMatch.Groups[2].Value
        baseline_chips_per_hand = [double]$bMatch.Groups[1].Value
        baseline_bb100 = [double]$bMatch.Groups[2].Value
    }
}

$baselineList = @(Resolve-Baselines -Raw $Baselines)
if ($baselineList.Count -le 0) {
    throw 'No baselines specified. Pass -Baselines "data\bp_ref_a.bin;data\bp_ref_b.bin".'
}

if ($Build) {
    Write-Host "[eval] running build.bat ..."
    & ".\build.bat"
    if ($LASTEXITCODE -ne 0) {
        throw "build.bat failed"
    }
}

if (-not (Test-Path -LiteralPath $MainExe)) {
    throw ("main executable not found: {0}" -f $MainExe)
}
if (-not (Test-Path -LiteralPath $Target)) {
    throw ("target blueprint not found: {0}" -f $Target)
}
if ($Abstraction.Trim().Length -gt 0 -and -not (Test-Path -LiteralPath $Abstraction)) {
    throw ("abstraction artifact not found: {0}" -f $Abstraction)
}
foreach ($b in $baselineList) {
    if (-not (Test-Path -LiteralPath $b)) {
        throw ("baseline blueprint not found: {0}" -f $b)
    }
}

Ensure-Directory -Path $Csv
Ensure-Directory -Path $Json

$runs = @()
$summaries = @()
$allPass = $true
$failedBaselines = @()

foreach ($baseline in $baselineList) {
    $bbVals = @()
    $scenarioRuns = @()
    for ($i = 0; $i -lt $Seeds; ++$i) {
        $seed = $SeedStart + [UInt64]$i
        $r = Invoke-MatchRun -ExePath $MainExe `
                             -TargetPath $Target `
                             -BaselinePath $baseline `
                             -AbstractionPath $Abstraction `
                             -ModeName $Mode `
                             -HandsCount $Hands `
                             -SeedValue $seed `
                             -SearchItersValue $SearchIters `
                             -SearchDepthValue $SearchDepth `
                             -SearchThreadsValue $SearchThreads `
                             -SearchPickValue $SearchPick `
                             -OfftreeModeValue $OfftreeMode `
                             -IgnoreAbstractionCompatValue:$IgnoreAbstractionCompat
        $row = [PSCustomObject]@{
            baseline = $baseline
            run_index = ($i + 1)
            seed = [UInt64]$seed
            hands = [UInt64]$Hands
            target_bb100 = [double]$r.target_bb100
            baseline_bb100 = [double]$r.baseline_bb100
            target_chips_per_hand = [double]$r.target_chips_per_hand
            baseline_chips_per_hand = [double]$r.baseline_chips_per_hand
        }
        $runs += $row
        $scenarioRuns += $row
        $bbVals += [double]$r.target_bb100
    }

    $mean = Get-Mean -Values $bbVals
    $std = Get-SampleStdDev -Values $bbVals
    if ($bbVals.Count -gt 1) {
        $ciHalf = 1.96 * $std / [Math]::Sqrt([double]$bbVals.Count)
    } else {
        $ciHalf = 0.0
    }
    $ciLow = $mean - $ciHalf
    $ciHigh = $mean + $ciHalf
    $pass = ($ciLow -gt $MinBb100)
    if (-not $pass) {
        $allPass = $false
        $failedBaselines += $baseline
    }

    $summaries += [PSCustomObject]@{
        baseline = $baseline
        n_runs = $bbVals.Count
        mean_target_bb100 = [double]$mean
        std_target_bb100 = [double]$std
        ci95_low_target_bb100 = [double]$ciLow
        ci95_high_target_bb100 = [double]$ciHigh
        gate_min_bb100 = [double]$MinBb100
        pass = [bool]$pass
    }
}

$csvRows = @()
foreach ($r in $runs) {
    $csvRows += [PSCustomObject]@{
        row_type = "run"
        baseline = $r.baseline
        run_index = $r.run_index
        seed = $r.seed
        hands = $r.hands
        target_bb100 = $r.target_bb100
        baseline_bb100 = $r.baseline_bb100
        mean_target_bb100 = ""
        std_target_bb100 = ""
        ci95_low_target_bb100 = ""
        ci95_high_target_bb100 = ""
        gate_min_bb100 = ""
        pass = ""
    }
}
foreach ($s in $summaries) {
    $csvRows += [PSCustomObject]@{
        row_type = "summary"
        baseline = $s.baseline
        run_index = ""
        seed = ""
        hands = ""
        target_bb100 = ""
        baseline_bb100 = ""
        mean_target_bb100 = $s.mean_target_bb100
        std_target_bb100 = $s.std_target_bb100
        ci95_low_target_bb100 = $s.ci95_low_target_bb100
        ci95_high_target_bb100 = $s.ci95_high_target_bb100
        gate_min_bb100 = $s.gate_min_bb100
        pass = $s.pass
    }
}

$csvLines = @()
$csvLines += '"row_type","baseline","run_index","seed","hands","target_bb100","baseline_bb100","mean_target_bb100","std_target_bb100","ci95_low_target_bb100","ci95_high_target_bb100","gate_min_bb100","pass"'
foreach ($r in $csvRows) {
    $f1 = Csv-Escape $r.row_type
    $f2 = Csv-Escape $r.baseline
    $f3 = Csv-Escape (Csv-Num $r.run_index)
    $f4 = Csv-Escape (Csv-Num $r.seed)
    $f5 = Csv-Escape (Csv-Num $r.hands)
    $f6 = Csv-Escape (Csv-Num $r.target_bb100)
    $f7 = Csv-Escape (Csv-Num $r.baseline_bb100)
    $f8 = Csv-Escape (Csv-Num $r.mean_target_bb100)
    $f9 = Csv-Escape (Csv-Num $r.std_target_bb100)
    $f10 = Csv-Escape (Csv-Num $r.ci95_low_target_bb100)
    $f11 = Csv-Escape (Csv-Num $r.ci95_high_target_bb100)
    $f12 = Csv-Escape (Csv-Num $r.gate_min_bb100)
    $f13 = Csv-Escape (Csv-Num $r.pass)
    $fields = @(
        $f1,
        $f2,
        $f3,
        $f4,
        $f5,
        $f6,
        $f7,
        $f8,
        $f9,
        $f10,
        $f11,
        $f12,
        $f13
    )
    $csvLines += ($fields -join ",")
}
$csvLines | Set-Content -Path $Csv -Encoding UTF8

$jsonObj = [PSCustomObject]@{
    meta = [PSCustomObject]@{
        generated_utc = (Get-Date).ToUniversalTime().ToString("o")
        target = $Target
        baselines = $baselineList
        abstraction = $Abstraction
        mode = $Mode
        hands = [UInt64]$Hands
        seeds = $Seeds
        seed_start = [UInt64]$SeedStart
        search_iters = [UInt64]$SearchIters
        search_depth = $SearchDepth
        search_threads = $SearchThreads
        search_pick = $SearchPick
        offtree_mode = $OfftreeMode
        gate_min_bb100 = [double]$MinBb100
        ignore_abstraction_compat = [bool]$IgnoreAbstractionCompat
    }
    runs = $runs
    summary = $summaries
    gates = [PSCustomObject]@{
        all_pass = [bool]$allPass
        failed_baselines = $failedBaselines
    }
}

$jsonObj | ConvertTo-Json -Depth 8 | Set-Content -Path $Json -Encoding UTF8

Write-Host ""
Write-Host "[eval] Summary"
foreach ($s in $summaries) {
    Write-Host ("  baseline={0}" -f $s.baseline)
    Write-Host ("    mean_bb100={0:N4} std={1:N4} ci95=[{2:N4}, {3:N4}] gate>{4:N4} pass={5}" -f
        $s.mean_target_bb100, $s.std_target_bb100, $s.ci95_low_target_bb100, $s.ci95_high_target_bb100, $s.gate_min_bb100, $s.pass)
}
Write-Host ("[eval] CSV:  {0}" -f $Csv)
Write-Host ("[eval] JSON: {0}" -f $Json)
Write-Host ("[eval] Overall pass: {0}" -f $allPass)

if ($allPass) {
    exit 0
}
exit 2
