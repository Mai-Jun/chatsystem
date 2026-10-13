# Enumerate all windows (visible or not) of im_client.exe with titles. ASCII only.
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class WE2 {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP cb, IntPtr lp);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public delegate bool EP(IntPtr h, IntPtr lp);
}
"@
$proc = Get-Process im_client -ErrorAction Stop | Select-Object -First 1
$out = New-Object System.Collections.ArrayList
$cb = [WE2+EP]{
  param($h, $lp)
  $p = 0
  [WE2]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null
  if ($p -eq $proc.Id) {
    $sb = New-Object System.Text.StringBuilder 256
    [WE2]::GetWindowText($h, $sb, 256) | Out-Null
    $vis = [WE2]::IsWindowVisible($h)
    $r = New-Object WE2+RECT
    [WE2]::GetWindowRect($h, [ref]$r) | Out-Null
    if ($sb.Length -gt 0) {
      $out.Add("vis=$vis rect=$($r.Left),$($r.Top),$($r.Right),$($r.Bottom) title=[$($sb.ToString())]") | Out-Null
    }
  }
  return $true
}
[WE2]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
foreach ($l in $out) { Write-Output $l }
