# Boot careos.iso headless and screenshot at given elapsed seconds.
#   tools\qemu-shot.ps1 -Shots 14,30 -Prefix before
param([int[]]$Shots = @(14), [string]$Prefix = "shot", [string]$OutDir = "shots")

Add-Type -AssemblyName System.Drawing
$qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$root = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force -Path "$root\$OutDir" | Out-Null

$p = Start-Process -FilePath $qemu -PassThru -ArgumentList @(
  "-m","4096M","-smp","4","-cdrom","$root\careos.iso","-no-reboot",
  "-serial","file:$root\$OutDir\$Prefix.log","-vga","std",
  "-machine","pc,usb=off","-display","none",
  "-qmp","tcp:127.0.0.1:4499,server,nowait")

Start-Sleep -Seconds 6
$cli = New-Object System.Net.Sockets.TcpClient("127.0.0.1",4499)
$ns = $cli.GetStream()
$sr = New-Object System.IO.StreamReader($ns)
$sw = New-Object System.IO.StreamWriter($ns); $sw.AutoFlush = $true
$sr.ReadLine() | Out-Null
$sw.WriteLine('{"execute":"qmp_capabilities"}'); $sr.ReadLine() | Out-Null

function Invoke-Qmp([string]$j) {
  $sw.WriteLine($j); $x = $sr.ReadLine()
  while ($x -match '"event"') { $x = $sr.ReadLine() }
  return $x
}

$elapsed = 6
foreach ($t in $Shots) {
  if ($t -gt $elapsed) { Start-Sleep -Seconds ($t - $elapsed); $elapsed = $t }
  $ppm = "$root\$OutDir\$Prefix-$t.ppm"
  Invoke-Qmp ('{"execute":"screendump","arguments":{"filename":"' + ($ppm -replace '\\','\\') + '"}}') | Out-Null
  # PPM (P6) -> PNG
  $b = [System.IO.File]::ReadAllBytes($ppm)
  $pos = 2; $tok = @()
  while ($tok.Count -lt 3) {
    while ([char]$b[$pos] -match '\s') { $pos++ }
    $s = ""; while (-not ([char]$b[$pos] -match '\s')) { $s += [char]$b[$pos]; $pos++ }
    $tok += $s
  }
  $pos++
  $w = [int]$tok[0]; $h = [int]$tok[1]
  $bmp = New-Object System.Drawing.Bitmap($w,$h,[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $d = $bmp.LockBits((New-Object System.Drawing.Rectangle(0,0,$w,$h)),
        [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
        [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $row = New-Object byte[] $d.Stride
  for ($y=0; $y -lt $h; $y++) {
    $src = $pos + $y*$w*3
    for ($x=0; $x -lt $w; $x++) {
      $row[$x*3]=$b[$src+$x*3+2]; $row[$x*3+1]=$b[$src+$x*3+1]; $row[$x*3+2]=$b[$src+$x*3]
    }
    [System.Runtime.InteropServices.Marshal]::Copy($row,0,[IntPtr]::Add($d.Scan0,$y*$d.Stride),$d.Stride)
  }
  $bmp.UnlockBits($d)
  $bmp.Save("$root\$OutDir\$Prefix-$t.png",[System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose(); Remove-Item $ppm -Force
  Write-Output "wrote $OutDir\$Prefix-$t.png"
}
$cli.Close(); Stop-Process -Id $p.Id -Force
