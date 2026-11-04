# Build resources/std.ico from resources/std-icon.png.
# Classic 32-bit DIB images so the resource compiler, Explorer, and
# Inno Setup all show the purple capsule. Not a PNG-inside-ICO file.
Add-Type -AssemblyName System.Drawing

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$srcPath = Join-Path $here 'std-icon.png'
$icoPath = Join-Path $here 'std.ico'
$src = [System.Drawing.Image]::FromFile($srcPath)
$sizes = @(16, 24, 32, 48, 64, 128, 256)

function Add-U16([System.Collections.Generic.List[byte]]$list, [int]$value) {
  $list.Add([byte]($value -band 255))
  $list.Add([byte](($value -shr 8) -band 255))
}
function Add-U32([System.Collections.Generic.List[byte]]$list, [int]$value) {
  $list.Add([byte]($value -band 255))
  $list.Add([byte](($value -shr 8) -band 255))
  $list.Add([byte](($value -shr 16) -band 255))
  $list.Add([byte](($value -shr 24) -band 255))
}

$images = New-Object System.Collections.Generic.List[byte[]]
foreach ($size in $sizes) {
  $bmp = New-Object System.Drawing.Bitmap $size, $size
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
  $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  $g.Clear([System.Drawing.Color]::Transparent)
  $g.DrawImage($src, 0, 0, $size, $size)
  $g.Dispose()
  $rect = New-Object System.Drawing.Rectangle 0, 0, $size, $size
  $locked = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $raw = New-Object byte[] ($locked.Stride * $size)
  [System.Runtime.InteropServices.Marshal]::Copy($locked.Scan0, $raw, 0, $raw.Length)
  $bmp.UnlockBits($locked)
  $bmp.Dispose()

  $xor = New-Object System.Collections.Generic.List[byte]
  for ($y = $size - 1; $y -ge 0; $y--) {
    $row = $y * $locked.Stride
    for ($x = 0; $x -lt $size; $x++) {
      $i = $row + ($x * 4)
      $xor.Add($raw[$i])
      $xor.Add($raw[$i + 1])
      $xor.Add($raw[$i + 2])
      $xor.Add($raw[$i + 3])
    }
  }
  $maskStride = [int]([math]::Ceiling($size / 32.0) * 4)
  $and = New-Object byte[] ($maskStride * $size)
  $dib = New-Object System.Collections.Generic.List[byte]
  Add-U32 $dib 40
  Add-U32 $dib $size
  Add-U32 $dib ($size * 2)
  Add-U16 $dib 1
  Add-U16 $dib 32
  Add-U32 $dib 0
  Add-U32 $dib ($xor.Count + $and.Length)
  Add-U32 $dib 0
  Add-U32 $dib 0
  Add-U32 $dib 0
  Add-U32 $dib 0
  $dib.AddRange($xor)
  $dib.AddRange($and)
  $images.Add($dib.ToArray())
}
$src.Dispose()

$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([uint16]0)
$bw.Write([uint16]1)
$bw.Write([uint16]$images.Count)
$offset = 6 + (16 * $images.Count)
for ($n = 0; $n -lt $images.Count; $n++) {
  $dim = 0
  if ($sizes[$n] -lt 256) { $dim = $sizes[$n] }
  $bw.Write([byte]$dim)
  $bw.Write([byte]$dim)
  $bw.Write([byte]0)
  $bw.Write([byte]0)
  $bw.Write([uint16]1)
  $bw.Write([uint16]32)
  $bw.Write([uint32]$images[$n].Length)
  $bw.Write([uint32]$offset)
  $offset += $images[$n].Length
}
foreach ($image in $images) { $bw.Write($image) }
$bw.Flush()
$bw.Dispose()
Write-Output $icoPath
