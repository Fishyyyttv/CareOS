# Crop-and-magnify a screenshot region so glyph collisions are readable.
#   tools\zoom.ps1 -In shots\bef-settings.png -Out shots\z.png -X 640 -Y 290 -W 520 -H 90 -Scale 3
param(
  [Parameter(Mandatory=$true)][string]$In,
  [Parameter(Mandatory=$true)][string]$Out,
  [int]$X = 0, [int]$Y = 0, [int]$W = 400, [int]$H = 200, [int]$Scale = 3
)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile((Resolve-Path $In))
$dst = New-Object System.Drawing.Bitmap(($W*$Scale), ($H*$Scale))
$g = [System.Drawing.Graphics]::FromImage($dst)
$g.InterpolationMode = 'NearestNeighbor'
$g.PixelOffsetMode = 'Half'
$g.DrawImage($src, (New-Object System.Drawing.Rectangle(0,0,($W*$Scale),($H*$Scale))),
                   (New-Object System.Drawing.Rectangle($X,$Y,$W,$H)), 'Pixel')
$dst.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $dst.Dispose(); $src.Dispose()
Write-Output "wrote $Out"
