# Drive the im_client window: move to anchor, optionally pin topmost, click at
# window-relative coordinates, or send keystrokes. ASCII only.
# Usage:
#   powershell -File scripts/win_ui_tool.ps1 -Action click -X 325 -Y 423
#   powershell -File scripts/win_ui_tool.ps1 -Action type -Text "pass123"
#   powershell -File scripts/win_ui_tool.ps1 -Action key -Text "{ENTER}"
# Multiple actions: pass a JSON-ish list via -Actions "click:325:423;type:pass123;key:{ENTER}"
param(
  [string]$Action = "",
  [int]$X = 0, [int]$Y = 0,
  [string]$Text = "",
  [string]$Actions = "",
  [string]$Title = ""
)
$ErrorActionPreference = "Stop"
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class U32 {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int t, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public delegate bool EnumProc(IntPtr h, IntPtr lp);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
Add-Type -AssemblyName System.Windows.Forms

[U32]::SetProcessDPIAware() | Out-Null
$proc = Get-Process -Name im_client -ErrorAction Stop | Select-Object -First 1
$all = New-Object System.Collections.ArrayList
$cb = [U32+EnumProc]{
  param($h, $lp)
  $p = 0
  [U32]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null
  if ($p -eq $proc.Id -and [U32]::IsWindowVisible($h)) {
    $sb = New-Object System.Text.StringBuilder 512
    [U32]::GetWindowText($h, $sb, 512) | Out-Null
    $t = $sb.ToString()
    if ($t.Length -gt 0) {
      $r = New-Object U32+RECT
      [U32]::GetWindowRect($h, [ref]$r) | Out-Null
      if (($r.Right - $r.Left) -gt 100) { $all.Add(@{H=$h; T=$t}) | Out-Null }
    }
  }
  return $true
}
[U32]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
$pick = $null
if ($Title -ne "") { foreach ($w in $all) { if ($w.T -match $Title) { $pick = $w; break } } }
else { if ($all.Count -gt 0) { $pick = $all[0] } }
if ($null -eq $pick) { Write-Output ("NO_WINDOW proc=" + $proc.Id + " count=" + $all.Count); exit 1 }

# Anchor the window so window-relative click coords are stable, pin topmost while acting.
$anchorX = 80; $anchorY = 60
[U32]::ShowWindow($pick.H, 9) | Out-Null
[U32]::SetWindowPos($pick.H, [IntPtr]::Zero, $anchorX, $anchorY, 0, 0, 0x0001 -bor 0x0010) | Out-Null
[U32]::SetWindowPos($pick.H, [IntPtr](-1), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0010) | Out-Null
[U32]::SetForegroundWindow($pick.H) | Out-Null
Start-Sleep -Milliseconds 500
$r = New-Object U32+RECT
[U32]::GetWindowRect($pick.H, [ref]$r) | Out-Null
Write-Output ("WINDOW " + $pick.T + " rect=" + $r.Left + "," + $r.Top + " size=" + ($r.Right-$r.Left) + "x" + ($r.Bottom-$r.Top))

function Do-Click([int]$wx, [int]$wy) {
  [U32]::SetCursorPos($r.Left + $wx, $r.Top + $wy) | Out-Null
  Start-Sleep -Milliseconds 120
  [U32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)  # LEFTDOWN
  Start-Sleep -Milliseconds 60
  [U32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)  # LEFTUP
  Start-Sleep -Milliseconds 150
}
function Do-DblClick([int]$wx, [int]$wy) {
  Do-Click $wx $wy
  Start-Sleep -Milliseconds 80
  Do-Click $wx $wy
}
function Do-Type([string]$s) {
  # clipboard paste: reliable for arbitrary text
  Set-Clipboard -Value $s
  Start-Sleep -Milliseconds 100
  [System.Windows.Forms.SendKeys]::SendWait("^a")
  Start-Sleep -Milliseconds 80
  [System.Windows.Forms.SendKeys]::SendWait("^v")
  Start-Sleep -Milliseconds 150
}
function Do-Key([string]$s) { [System.Windows.Forms.SendKeys]::SendWait($s); Start-Sleep -Milliseconds 200 }

if ($Actions -ne "") {
  foreach ($piece in $Actions.Split(";")) {
    if ($piece -eq "") { continue }
    $parts = $piece.Split(":", 3)
    switch ($parts[0]) {
      "click" { Do-Click ([int]$parts[1]) ([int]$parts[2]); Write-Output ("click " + $parts[1] + "," + $parts[2]) }
      "dblclick" { Do-DblClick ([int]$parts[1]) ([int]$parts[2]); Write-Output ("dblclick " + $parts[1] + "," + $parts[2]) }
      "type"  { Do-Type $parts[1]; Write-Output ("type " + $parts[1]) }
      "key"   { Do-Key $parts[1]; Write-Output ("key " + $parts[1]) }
      "wait"  { Start-Sleep -Milliseconds ([int]$parts[1]); Write-Output ("wait " + $parts[1]) }
    }
  }
} else {
  switch ($Action) {
    "click" { Do-Click $X $Y; Write-Output ("click " + $X + "," + $Y) }
    "type"  { Do-Type $Text; Write-Output ("type " + $Text) }
    "key"   { Do-Key $Text; Write-Output ("key " + $Text) }
    default { Write-Output "no action" }
  }
}
# Unpin topmost (leave it where it is)
[U32]::SetWindowPos($pick.H, [IntPtr](-2), 0, 0, 0, 0, 0x0001 -bor 0x0002 -bor 0x0010) | Out-Null
Write-Output "DONE"
