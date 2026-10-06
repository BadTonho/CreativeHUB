$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$iconDirectory = Join-Path $PSScriptRoot '..\docs\assets\app-icons'
$iconDirectory = [System.IO.Path]::GetFullPath($iconDirectory)
$iconNames = @('video-editor', 'image-editor', 'motion-studio', 'hub')
$iconSizes = @(16, 32, 48, 64, 128, 256)

foreach ($iconName in $iconNames) {
    $sourcePath = Join-Path $iconDirectory "$iconName.png"
    $outputPath = Join-Path $iconDirectory "$iconName.ico"
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Icon source not found: $sourcePath"
    }

    $sourceImage = [System.Drawing.Image]::FromFile($sourcePath)
    $frames = [System.Collections.Generic.List[object]]::new()
    try {
        foreach ($size in $iconSizes) {
            $bitmap = [System.Drawing.Bitmap]::new(
                $size,
                $size,
                [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            $pngStream = [System.IO.MemoryStream]::new()
            try {
                $graphics.Clear([System.Drawing.Color]::Transparent)
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.DrawImage($sourceImage, 0, 0, $size, $size)
                $bitmap.Save($pngStream, [System.Drawing.Imaging.ImageFormat]::Png)
                [void]$frames.Add([PSCustomObject]@{
                    Size = $size
                    Data = $pngStream.ToArray()
                })
            } finally {
                $pngStream.Dispose()
                $graphics.Dispose()
                $bitmap.Dispose()
            }
        }

        $iconStream = [System.IO.MemoryStream]::new()
        $writer = [System.IO.BinaryWriter]::new($iconStream)
        try {
            $writer.Write([UInt16]0) # Reserved.
            $writer.Write([UInt16]1) # Icon resource.
            $writer.Write([UInt16]$frames.Count)

            $imageOffset = 6 + (16 * $frames.Count)
            foreach ($frame in $frames) {
                $dimension = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
                $writer.Write([Byte]$dimension)
                $writer.Write([Byte]$dimension)
                $writer.Write([Byte]0) # Palette colors; zero for true color.
                $writer.Write([Byte]0) # Reserved.
                $writer.Write([UInt16]1) # Color planes.
                $writer.Write([UInt16]32) # Bits per pixel.
                $writer.Write([UInt32]$frame.Data.Length)
                $writer.Write([UInt32]$imageOffset)
                $imageOffset += $frame.Data.Length
            }

            foreach ($frame in $frames) {
                $writer.Write([Byte[]]$frame.Data)
            }
            $writer.Flush()
            [System.IO.File]::WriteAllBytes($outputPath, $iconStream.ToArray())
        } finally {
            $writer.Dispose()
            $iconStream.Dispose()
        }
    } finally {
        $sourceImage.Dispose()
    }
}
