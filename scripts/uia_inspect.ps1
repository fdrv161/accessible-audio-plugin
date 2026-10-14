<#
.SYNOPSIS
  Shows what screen readers get from a plugin window through UI Automation:
  names, roles, values and states, focusable elements without a name, and the
  focus changes while you use the plugin. Windows only, nothing to install.

.DESCRIPTION
  Uses the UI Automation client built into Windows (.NET Framework), the same
  data NVDA and Narrator read. Output is plain lines, one element per line,
  without colours or drawing characters, so it is easy to hear with a screen reader.

  Modes:
    List   lists JUCE windows (plugin editors, standalone apps) on screen.
    Tree   prints the accessibility tree of one window and reports problems.
    Watch  prints every focus change and every change of the focused element's
           name or value for some seconds: roughly what NVDA announces.

  A JUCE window is found by its window class name, which starts with "JUCE_".
  For other frameworks pass -Title to inspect any window.

  Exit codes: 0 done, no problems; 1 problems found (Tree); 2 nothing found or bad arguments.

.PARAMETER Mode
  List, Tree or Watch. Default: Tree.
.PARAMETER Process
  Process name without .exe, for example reaper. Limits the search to that program.
.PARAMETER Title
  Part of a top-level window title. If that window contains JUCE windows, the first
  one is used; otherwise the window itself is inspected.
.PARAMETER Index
  Which JUCE window to use when several are found (numbers from -Mode List). Default 1.
.PARAMETER Depth
  Maximum tree depth for Tree. Default 30.
.PARAMETER Seconds
  How long Watch runs. Default 60.
.PARAMETER Delay
  Seconds to wait before starting, to switch to the plugin window. Default 0.
.PARAMETER LogFile
  Also append all output to this text file (UTF-8).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File uia_inspect.ps1 -Mode List
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File uia_inspect.ps1 -Mode Tree -Process reaper
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File uia_inspect.ps1 -Mode Watch -Seconds 90 -Delay 5 -LogFile focus-log.txt
  Start it, switch to the plugin within 5 seconds and use it; then read focus-log.txt.
#>
[CmdletBinding()]
param(
    [ValidateSet('List', 'Tree', 'Watch')] [string] $Mode = 'Tree',
    [string] $Process = '',
    [string] $Title = '',
    [int] $Index = 1,
    [int] $Depth = 30,
    [int] $Seconds = 60,
    [int] $Delay = 0,
    [string] $LogFile = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$AE = [System.Windows.Automation.AutomationElement]

function Write-Line([string] $text) {
    Write-Output $text
    if ($LogFile) { Add-Content -LiteralPath $LogFile -Value $text -Encoding UTF8 }
}

function Get-Pattern($element, $pattern) {
    $result = $null
    if ($element.TryGetCurrentPattern($pattern, [ref] $result)) { return $result }
    return $null
}

function Get-ProcessName([int] $id) {
    try { return (Get-Process -Id $id).ProcessName } catch { return "process $id" }
}

# One spoken-style line: role, name, value and states, plus problems.
function Describe($element) {
    $c = $element.Current
    $role = $c.ControlType.ProgrammaticName -replace '^ControlType\.', ''
    $name = $c.Name
    $parts = @()
    if ($name) { $parts += "$role `"$name`"" } else { $parts += "$role (no name)" }

    $value = Get-Pattern $element ([System.Windows.Automation.ValuePattern]::Pattern)
    $range = Get-Pattern $element ([System.Windows.Automation.RangeValuePattern]::Pattern)
    $toggle = Get-Pattern $element ([System.Windows.Automation.TogglePattern]::Pattern)
    $selItem = Get-Pattern $element ([System.Windows.Automation.SelectionItemPattern]::Pattern)
    $expand = Get-Pattern $element ([System.Windows.Automation.ExpandCollapsePattern]::Pattern)

    $valueText = ''
    if ($value) { $valueText = $value.Current.Value; $parts += "value `"$valueText`"" }
    if ($range) {
        $r = $range.Current
        $parts += "range value $($r.Value) of $($r.Minimum) to $($r.Maximum)"
    }
    if ($toggle) { $parts += "toggle $($toggle.Current.ToggleState)" }
    if ($selItem -and $selItem.Current.IsSelected) { $parts += 'selected' }
    if ($expand) { $parts += "$($expand.Current.ExpandCollapseState)" }
    if ($c.IsKeyboardFocusable) { $parts += 'focusable' }
    if ($c.HasKeyboardFocus) { $parts += 'HAS FOCUS' }
    if (-not $c.IsEnabled) { $parts += 'disabled' }
    if ($c.IsOffscreen) { $parts += 'offscreen' }
    if ($c.HelpText) { $parts += "help `"$($c.HelpText)`"" }

    $problems = @()
    if ($c.IsKeyboardFocusable -and $c.IsEnabled -and -not $name) {
        $problems += 'PROBLEM: focusable element without a name (defect CTL-01)'
    }
    if ($range -and -not $value) {
        $problems += 'NOTE: number only, no text value: screen readers may read a bare number (CTL-02)'
    }
    if ($value -and $range -and $valueText -match '^-?[0-9.]+$') {
        $problems += 'NOTE: value text is a bare number without units (CTL-02)'
    }

    return [pscustomobject]@{ Line = ($parts -join ', ') + '.'; Problems = $problems; Name = $name; Value = $valueText;
                              Focusable = [bool] $c.IsKeyboardFocusable }
}

# HWND-backed elements only: fast search for JUCE windows inside host windows.
function Find-JuceWindows {
    $hwndWalker = New-Object System.Windows.Automation.TreeWalker (
        New-Object System.Windows.Automation.NotCondition (
            New-Object System.Windows.Automation.PropertyCondition ($AE::NativeWindowHandleProperty, 0)))

    $tops = $AE::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children,
                                     [System.Windows.Automation.Condition]::TrueCondition)
    $found = New-Object System.Collections.ArrayList
    foreach ($top in $tops) {
        try {
            if ($Process -and (Get-ProcessName $top.Current.ProcessId) -ne $Process) { continue }
            if ($Title -and $top.Current.Name -notlike "*$Title*") { continue }
        } catch { continue }
        $stack = New-Object System.Collections.Stack
        $stack.Push(@($top, 0, $false))
        while ($stack.Count -gt 0) {
            $item = $stack.Pop()
            $el = $item[0]; $level = $item[1]; $insideJuce = $item[2]
            $isJuce = $false
            try { $isJuce = $el.Current.ClassName -like 'JUCE_*' } catch { }
            if ($isJuce -and -not $insideJuce) { [void] $found.Add($el) }
            if ($level -ge 12) { continue }
            try { $child = $hwndWalker.GetFirstChild($el) } catch { $child = $null }
            while ($child -ne $null) {
                $stack.Push(@($child, ($level + 1), ($insideJuce -or $isJuce)))
                try { $child = $hwndWalker.GetNextSibling($child) } catch { $child = $null }
            }
        }
    }
    return ,$found
}

function Get-TitledWindow {
    $tops = $AE::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children,
                                     [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($top in $tops) {
        try {
            if ($Process -and (Get-ProcessName $top.Current.ProcessId) -ne $Process) { continue }
            if ($top.Current.Name -like "*$Title*") { return $top }
        } catch { }
    }
    return $null
}

if ($Delay -gt 0) {
    Write-Line "Waiting $Delay seconds. Switch to the plugin window now."
    Start-Sleep -Seconds $Delay
}

if ($Mode -eq 'Watch') {
    Write-Line "Watching focus for $Seconds seconds. Use the plugin; every focus, name or value change is listed."
    $end = (Get-Date).AddSeconds($Seconds)
    $lastId = ''; $lastName = $null; $lastValue = $null
    while ((Get-Date) -lt $end) {
        $focused = $null
        try { $focused = $AE::FocusedElement } catch { }
        if ($focused -ne $null) {
            try {
                $id = ($focused.GetRuntimeId() -join '.')
                $d = Describe $focused
                $time = (Get-Date).ToString('HH:mm:ss')
                if ($id -ne $lastId) {
                    $app = Get-ProcessName $focused.Current.ProcessId
                    Write-Line "$time Focus in ${app}: $($d.Line)"
                    foreach ($p in $d.Problems) { Write-Line "  $p" }
                } elseif ($d.Name -ne $lastName) {
                    Write-Line "$time Name changed: `"$($d.Name)`""
                } elseif ($d.Value -ne $lastValue) {
                    Write-Line "$time Value changed: `"$($d.Value)`""
                }
                $lastId = $id; $lastName = $d.Name; $lastValue = $d.Value
            } catch { }
        }
        Start-Sleep -Milliseconds 150
    }
    Write-Line 'Watch finished.'
    exit 0
}

$windows = Find-JuceWindows

if ($Mode -eq 'List') {
    if ($windows.Count -eq 0) {
        Write-Line 'No JUCE windows found. Open the plugin window (or the standalone app) and try again, or use -Title for other frameworks.'
        exit 2
    }
    $n = 0
    foreach ($w in $windows) {
        $n++
        $c = $w.Current
        Write-Line "Window $n. Name `"$($c.Name)`", program $(Get-ProcessName $c.ProcessId), class $($c.ClassName)."
    }
    Write-Line "Found $n JUCE windows. Use -Mode Tree -Index N to inspect one."
    exit 0
}

# Tree
$target = $null
if ($windows.Count -ge $Index -and $Index -ge 1) {
    $target = $windows[$Index - 1]
} elseif ($Title) {
    $target = Get-TitledWindow
}
if ($target -eq $null) {
    Write-Line 'No window to inspect. Open the plugin window, then use -Mode List, or pass -Title with part of the window title.'
    exit 2
}

Write-Line "Tree of `"$($target.Current.Name)`" in $(Get-ProcessName $target.Current.ProcessId). Control view, depth up to $Depth."
$walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
$count = 0; $focusable = 0; $problemCount = 0; $noteCount = 0
$names = @{}
$stack = New-Object System.Collections.Stack
$stack.Push(@($target, 1))
$ordered = New-Object System.Collections.ArrayList
# Depth-first, children in screen-reader order.
while ($stack.Count -gt 0) {
    $item = $stack.Pop()
    $el = $item[0]; $level = $item[1]
    [void] $ordered.Add(@($el, $level))
    if ($level -ge $Depth) { continue }
    $children = New-Object System.Collections.ArrayList
    try { $child = $walker.GetFirstChild($el) } catch { $child = $null }
    while ($child -ne $null) {
        [void] $children.Add($child)
        try { $child = $walker.GetNextSibling($child) } catch { $child = $null }
    }
    for ($i = $children.Count - 1; $i -ge 0; $i--) { $stack.Push(@($children[$i], ($level + 1))) }
}

foreach ($entry in $ordered) {
    $el = $entry[0]; $level = $entry[1]
    try { $d = Describe $el } catch { continue }
    $count++
    if ($d.Focusable) {
        $focusable++
        if ($d.Name) { $names[$d.Name] = 1 + [int] $names[$d.Name] }
    }
    Write-Line "Level $level. $($d.Line)"
    foreach ($p in $d.Problems) {
        Write-Line "  $p"
        if ($p.StartsWith('PROBLEM')) { $problemCount++ } else { $noteCount++ }
    }
}

foreach ($key in $names.Keys) {
    if ($names[$key] -gt 1) {
        Write-Line "NOTE: $($names[$key]) focusable elements share the name `"$key`": the user cannot tell them apart (CTL-04)."
        $noteCount++
    }
}

Write-Line "Summary: $count elements, $focusable focusable, $problemCount problems, $noteCount notes."
Write-Line 'Not visible to this tool: JUCE descriptions (UIA FullDescription) and spoken notifications. Check those with NVDA.'
if ($problemCount -gt 0) { exit 1 }
exit 0
