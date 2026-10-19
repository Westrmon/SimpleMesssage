# SimpleMessage — 由 logo.png 生成多尺寸应用图标（Windows）

param(
  [string]$Source = 'assets/logo512x512.png',
  [string]$Out = 'src/host/sm_host.ico'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# 每种尺寸各写一条。16/20/24 是托盘在 100%/125%/150% 下的实际像素尺寸，
# 32/40 是标题栏与任务栏，48 是 Alt+Tab，其余供资源管理器与"大图标"视图。
$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
# 小尺寸用位图条目（BMP），大尺寸用 PNG 条目：Vista 之后的系统两种都认，
# 但 PNG 只在较大尺寸上有意义（小图 PNG 的头开销比像素还大）。
$png_at = 64
# 裁出图形之后，把它放进一张 $art_canvas 见方的画布，四周各留 $art_margin 的
# 边距。2% 是"图案看起来填满了图标、又没有顶到边"的经验值。
$art_canvas = 1024
$art_margin = 0.02

# alpha 高于它才算"有内容"。不用 0：JPEG 转来的 PNG、或做过抗锯齿的描边，
# 边缘一圈会残留 1~3 的低 alpha 噪点，按 0 算边界会把整张画布又框回来。
$alpha_floor = 8

function Get-AlphaBounds([System.Drawing.Bitmap]$bmp) {
  # 图形在画布里的实际外接矩形。逐像素扫一遍：这张图只在生成图标时读一次，
  # 一百多万次比较对 PowerShell 只是一瞬间，不值得为它引入更聪明的做法。
  $rect = New-Object System.Drawing.Rectangle 0, 0, $bmp.Width, $bmp.Height
  $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  try {
    $stride = $data.Stride
    [byte[]]$bytes = New-Object byte[] ($stride * $bmp.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
  } finally {
    $bmp.UnlockBits($data)
  }
  $min_x = $bmp.Width; $min_y = $bmp.Height; $max_x = -1; $max_y = -1
  for ($y = 0; $y -lt $bmp.Height; $y++) {
    $row = $y * $stride
    for ($x = 0; $x -lt $bmp.Width; $x++) {
      if ($bytes[$row + $x * 4 + 3] -gt $alpha_floor) {
        if ($x -lt $min_x) { $min_x = $x }
        if ($x -gt $max_x) { $max_x = $x }
        if ($y -lt $min_y) { $min_y = $y }
        if ($y -gt $max_y) { $max_y = $y }
      }
    }
  }
  if ($max_x -lt 0) { throw "源图整张都是透明的，没有可裁的内容：$Source" }
  return New-Object System.Drawing.Rectangle $min_x, $min_y, ($max_x - $min_x + 1), ($max_y - $min_y + 1)
}

function New-ArtSquare([System.Drawing.Bitmap]$bmp, [System.Drawing.Rectangle]$bounds, [int]$canvas) {
  # 把图形裁出来、按比例放进一张 $canvas 见方的透明画布中央，四周留 $art_margin
  # 比例的边距。之后每个尺寸都从这张画布缩，比例与边距只需要在这里定一次。
  #
  # 【为什么不直接拉伸填满】图形是 778×801，直接填进正方形会压扁。等比缩放再
  # 居中则四周留一条极窄的空隙 —— 那正是图标该有的样子。
  $pad = [Math]::Max(1, [int][Math]::Round($canvas * $art_margin))
  $box = $canvas - 2 * $pad
  $scale = [Math]::Min($box / $bounds.Width, $box / $bounds.Height)
  $w = [int][Math]::Round($bounds.Width * $scale)
  $h = [int][Math]::Round($bounds.Height * $scale)
  $dst = New-Object System.Drawing.Bitmap $canvas, $canvas, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($dst)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  # 往透明底上画用 SourceCopy：SourceOver 会把源与"透明黑"混合，半透明的边缘
  # 因此被压暗一圈，缩到 16px 时那圈暗边比图形本身还显眼。
  $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
  $dx = [int][Math]::Floor(($canvas - $w) / 2)
  $dy = [int][Math]::Floor(($canvas - $h) / 2)
  $g.DrawImage($bmp, (New-Object System.Drawing.Rectangle $dx, $dy, $w, $h), $bounds, [System.Drawing.GraphicsUnit]::Pixel)
  $g.Dispose()
  return $dst
}

function New-ScaledBitmap([System.Drawing.Bitmap]$src, [int]$size) {
  # 【先反复折半再收尾】一次性把 1254 缩到 16 会让双三次插值只看邻近几个像素，
  # 细节被采样成噪点；逐级折半等于每级都做一次低通，边缘才干净。
  $cur = $src
  while ($cur.Width -gt 2 * $size) {
    $half = New-Object System.Drawing.Bitmap ([int][Math]::Max($size, $cur.Width / 2)), ([int][Math]::Max($size, $cur.Height / 2)), ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($half)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.DrawImage($cur, 0, 0, $half.Width, $half.Height)
    $g.Dispose()
    if ($cur -ne $src) { $cur.Dispose() }
    $cur = $half
  }
  $dst = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($dst)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
  $g.DrawImage($cur, 0, 0, $size, $size)
  $g.Dispose()
  if ($cur -ne $src) { $cur.Dispose() }
  return $dst
}

function Get-BgraRows([System.Drawing.Bitmap]$bmp) {
  # 取 32bpp 像素，并翻成【自下而上】—— ICO 里的位图条目与 BMP 一样是倒序存的。
  $rect = New-Object System.Drawing.Rectangle 0, 0, $bmp.Width, $bmp.Height
  $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  try {
    $stride = $data.Stride
    $row = $bmp.Width * 4
    [byte[]]$bytes = New-Object byte[] ($row * $bmp.Height)
    for ($y = 0; $y -lt $bmp.Height; $y++) {
      $src_off = ($bmp.Height - 1 - $y) * $stride
      [System.Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($data.Scan0, $src_off), $bytes, $y * $row, $row)
    }
    # 【逗号不能省】函数返回一个数组时，PowerShell 会把它拆成一个个元素送进管道，
    # 调用方收到的是 Object[] 而不是 byte[] —— 那样再交给 BinaryWriter.Write
    # 只会静默地写出一个字节。前置逗号让整块数组作为一个对象返回。
    return ,$bytes
  } finally {
    $bmp.UnlockBits($data)
  }
}

function New-BmpEntry([System.Drawing.Bitmap]$bmp) {
  $w = $bmp.Width
  $h = $bmp.Height
  [byte[]]$pixels = Get-BgraRows $bmp
  $ms = New-Object System.IO.MemoryStream
  $bw = New-Object System.IO.BinaryWriter $ms
  # BITMAPINFOHEADER：高度写两倍，因为条目里紧随其后的是 AND 掩码那半张图。
  $bw.Write([uint32]40); $bw.Write([int32]$w); $bw.Write([int32]($h * 2))
  $bw.Write([uint16]1); $bw.Write([uint16]32); $bw.Write([uint32]0)
  $bw.Write([uint32]($w * $h * 4)); $bw.Write([int32]0); $bw.Write([int32]0)
  $bw.Write([uint32]0); $bw.Write([uint32]0)
  $bw.Write($pixels)
  # AND 掩码：全 0 = "每一格都不透明"。32bpp 条目真正生效的是 alpha 通道，
  # 掩码只是留给还在读它的旧代码，因此可以全零（也必须是全零，否则被扣出洞）。
  $mask_row = [int](([Math]::Floor(($w + 31) / 32)) * 4)
  $bw.Write((New-Object byte[] ($mask_row * $h)))
  $bw.Flush()
  [byte[]]$bytes = $ms.ToArray()
  $bw.Dispose(); $ms.Dispose()
  return ,$bytes
}

function New-PngEntry([System.Drawing.Bitmap]$bmp) {
  $ms = New-Object System.IO.MemoryStream
  $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
  [byte[]]$bytes = $ms.ToArray()
  $ms.Dispose()
  return ,$bytes
}

if (-not (Test-Path $Source)) { throw "找不到图标源文件：$Source" }
$src = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Source).Path)

$src_w = $src.Width
$src_h = $src.Height
$bounds = Get-AlphaBounds $src
Write-Host ("源图 {0}×{1}，图形实际占 {2}×{3}（x[{4}..{5}] y[{6}..{7}]），按此裁边" -f `
  $src_w, $src_h, $bounds.Width, $bounds.Height, `
  $bounds.Left, $bounds.Right, $bounds.Top, $bounds.Bottom)

# 裁边这一步只在这里做一次，之后所有尺寸都从裁好的画布缩 —— 每个尺寸各裁一次
# 会得到九个略有出入的边界，托盘与任务栏上的图案就不再严丝合缝地对齐了。
$art = New-ArtSquare $src $bounds $art_canvas
$src.Dispose()

$entries = @()
foreach ($size in $sizes) {
  $bmp = New-ScaledBitmap $art $size
  [byte[]]$data = if ($size -ge $png_at) { New-PngEntry $bmp } else { New-BmpEntry $bmp }
  $bmp.Dispose()
  $entries += [pscustomobject]@{ Size = $size; Data = $data }
}
$art.Dispose()

$out_path = [System.IO.Path]::GetFullPath($Out)
$dir = [System.IO.Path]::GetDirectoryName($out_path)
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }

$fs = [System.IO.File]::Create($out_path)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$entries.Count)
# 目录在先、图像数据在后，因此每条记录的偏移是"目录大小 + 前面所有数据之和"。
$offset = 6 + 16 * $entries.Count
foreach ($e in $entries) {
  $b = if ($e.Size -ge 256) { 0 } else { $e.Size }   # 256 在目录里记作 0
  $bw.Write([byte]$b); $bw.Write([byte]$b)           # 宽、高
  $bw.Write([byte]0); $bw.Write([byte]0)             # 调色板色数、保留
  $bw.Write([uint16]1); $bw.Write([uint16]32)        # 平面数、每像素位数
  $bw.Write([uint32]$e.Data.Length); $bw.Write([uint32]$offset)
  $offset += $e.Data.Length
}
foreach ($e in $entries) { $bw.Write($e.Data) }
$bw.Flush(); $bw.Dispose(); $fs.Dispose()

Write-Host ("已写出 {0}（{1} 种尺寸：{2}）" -f $out_path, $entries.Count, ($sizes -join ', '))
