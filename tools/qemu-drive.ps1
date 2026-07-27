# Boot careos.iso headless and drive it over QMP from a simple step script.
#
#   tools\qemu-drive.ps1 -Script tools\drive-tour.txt -Prefix before
#
# Step-script grammar (one step per line, '#' starts a comment):
#   wait <seconds>          sleep
#   shot <name>             screendump -> shots\<Prefix>-<name>.png
#   type <text>             type literal text (rest of line, spaces kept)
#   key <qcode>[+<qcode>]   send one key chord, e.g. `key ret`, `key ctrl+c`
#   rel <dx> <dy>           relative PS/2 mouse move
#   click                   left press + release at the current cursor spot
#
# The PS/2 mouse is relative-only (-machine pc,usb=off => no USB tablet), so
# move deltas are measured from the cursor's parked position at SCREEN_W/2,
# SCREEN_H/2. Move in chunks; the driver applies acceleration.
param(
  [Parameter(Mandatory=$true)][string]$Script,
  [string]$Prefix = "shot",
  [string]$OutDir = "shots"
)

Add-Type -AssemblyName System.Drawing
$qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$root = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force -Path "$root\$OutDir" | Out-Null

$p = Start-Process -FilePath $qemu -PassThru -ArgumentList @(
  "-m","4096M","-smp","4","-cdrom","$root\careos.iso","-no-reboot",
  "-serial","file:$root\$OutDir\$Prefix.log","-vga","std",
  "-machine","pc,usb=off","-display","none",
  "-qmp","tcp:127.0.0.1:4499,server,nowait")

Start-Sleep -Seconds 4
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

# --- char -> qcode -----------------------------------------------------------
$plain = @{}
foreach ($c in [char[]]'abcdefghijklmnopqrstuvwxyz') { $plain["$c"] = "$c" }
foreach ($c in [char[]]'0123456789') { $plain["$c"] = "$c" }
$plain[' ']='spc'; $plain['.']='dot'; $plain[',']='comma'; $plain['-']='minus'
$plain['/']='slash'; $plain[';']='semicolon'; $plain["'"]='apostrophe'
$plain['[']='bracket_left'; $plain[']']='bracket_right'; $plain['\']='backslash'
$plain['=']='equal'; $plain['`']='grave_accent'

$shifted = @{}
foreach ($c in [char[]]'ABCDEFGHIJKLMNOPQRSTUVWXYZ') { $shifted["$c"] = "$c".ToLower() }
$shifted['!']='1'; $shifted['@']='2'; $shifted['#']='3'; $shifted['$']='4'
$shifted['%']='5'; $shifted['^']='6'; $shifted['&']='7'; $shifted['*']='8'
$shifted['(']='9'; $shifted[')']='0'; $shifted['_']='minus'; $shifted['+']='equal'
$shifted['{']='bracket_left'; $shifted['}']='bracket_right'; $shifted['|']='backslash'
$shifted[':']='semicolon'; $shifted['"']='apostrophe'; $shifted['<']='comma'
$shifted['>']='dot'; $shifted['?']='slash'; $shifted['~']='grave_accent'

function Send-Key1([string]$code, [bool]$down) {
  $j = '{"execute":"input-send-event","arguments":{"events":[{"type":"key","data":' +
       '{"down":' + $(if ($down) {"true"} else {"false"}) +
       ',"key":{"type":"qcode","data":"' + $code + '"}}}]}}'
  Invoke-Qmp($j) | Out-Null
}

# Explicit down/up per key. `send-key` bundles a chord into one hold with no
# gap, and the PS/2 driver's shift tracking misses it, so shifted characters
# came through unshifted. Separate events with a real gap fix that.
function Send-Chord([string[]]$codes) {
  foreach ($c in $codes) { Send-Key1 $c $true; Start-Sleep -Milliseconds 25 }
  for ($i = $codes.Count - 1; $i -ge 0; $i--) { Send-Key1 $codes[$i] $false; Start-Sleep -Milliseconds 25 }
  Start-Sleep -Milliseconds 30
}

function Send-Text([string]$s) {
  foreach ($ch in [char[]]$s) {
    $k = "$ch"
    # Uppercase must be tested first and with -cmatch: PowerShell hashtable
    # lookups are case-INSENSITIVE, so $plain['C'] happily returns the 'c'
    # qcode and every capital letter would be typed in lower case.
    if ($k -cmatch '^[A-Z]$')          { Send-Chord @('shift', $k.ToLower()) }
    elseif ($plain.ContainsKey($k))    { Send-Chord @($plain[$k]) }
    elseif ($shifted.ContainsKey($k))  { Send-Chord @('shift', $shifted[$k]) }
    else { Write-Warning "no qcode for '$k'" }
  }
}

function Send-Rel([int]$dx,[int]$dy) {
  # Zero-valued rel events are dropped by QEMU's input layer, and a batch that
  # contains one loses the whole packet, so only emit the axes that moved.
  $parts = @()
  if ($dx -ne 0) { $parts += '{"type":"rel","data":{"axis":"x","value":' + $dx + '}}' }
  if ($dy -ne 0) { $parts += '{"type":"rel","data":{"axis":"y","value":' + $dy + '}}' }
  if ($parts.Count -eq 0) { return }
  Invoke-Qmp('{"execute":"input-send-event","arguments":{"events":[' + ($parts -join ',') + ']}}') | Out-Null
  Start-Sleep -Milliseconds 120
}

function Send-Click() {
  Invoke-Qmp('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"left"}}]}}') | Out-Null
  Start-Sleep -Milliseconds 120
  Invoke-Qmp('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"left"}}]}}') | Out-Null
  Start-Sleep -Milliseconds 300
}

# The GUI parks the cursor at SCREEN_W/2, SCREEN_H/2 on the login screen, so
# absolute targets can be reached by tracking our own deltas from there.
# gui/mouse.c negates both axes ("flipped to match host pointer") and clamps
# each PS/2 packet to MOUSE_MAX_RAW_DELTA (24px), hence the sign flip and the
# 20px chunking below.
$script:curX = 960
$script:curY = 540

function Move-To([int]$tx,[int]$ty) {
  while ($script:curX -ne $tx -or $script:curY -ne $ty) {
    $dx = [Math]::Max([Math]::Min($tx - $script:curX, 20), -20)
    $dy = [Math]::Max([Math]::Min($ty - $script:curY, 20), -20)
    Send-Rel (-$dx) (-$dy)
    $script:curX += $dx; $script:curY += $dy
  }
}

function Save-Shot([string]$name) {
  $ppm = "$root\$OutDir\$Prefix-$name.ppm"
  Invoke-Qmp('{"execute":"screendump","arguments":{"filename":"' + ($ppm -replace '\\','\\') + '"}}') | Out-Null
  Start-Sleep -Milliseconds 400
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
  $bmp.Save("$root\$OutDir\$Prefix-$name.png",[System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose(); Remove-Item $ppm -Force
  Write-Output "wrote $OutDir\$Prefix-$name.png"
}

foreach ($line in (Get-Content $Script)) {
  $t = $line.Trim()
  if ($t -eq "" -or $t.StartsWith("#")) { continue }
  $verb, $rest = ($t -split ' ', 2)
  switch ($verb) {
    "wait"  { Start-Sleep -Seconds ([int]$rest) }
    "shot"  { Save-Shot $rest.Trim() }
    "type"  { Send-Text $rest }
    "key"   { Send-Chord ($rest.Trim() -split '\+') }
    "rel"   { $a = $rest.Trim() -split '\s+'; Send-Rel ([int]$a[0]) ([int]$a[1]) }
    "moveto" { $a = $rest.Trim() -split '\s+'; Move-To ([int]$a[0]) ([int]$a[1]) }
    "origin" { $a = $rest.Trim() -split '\s+'; $script:curX = [int]$a[0]; $script:curY = [int]$a[1] }
    "click" { Send-Click }
    default { Write-Warning "unknown step: $t" }
  }
}

$cli.Close(); Stop-Process -Id $p.Id -Force
