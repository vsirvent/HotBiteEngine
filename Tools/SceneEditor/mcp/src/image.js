'use strict';

// Downscales an editor screenshot before it goes to the model. The backbuffer is
// the editor's full window (2560x1377 on the development machine), several MB as a
// PNG and larger than a vision model looks at anyway - it is resized to fit
// `maxSize` on its longer side and re-encoded as JPEG.
//
// System.Drawing through PowerShell, because it ships with Windows and this server
// deliberately has no npm dependencies (the machine's Node is 14, which the MCP
// SDK does not support). Paths travel in environment variables so nothing needs
// quoting for the PowerShell parser.

const { execFile } = require('child_process');
const fs = require('fs');

const SCRIPT = `
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($env:HB_SRC)
try {
	$max = [int]$env:HB_MAX
	$scale = [Math]::Min(1.0, $max / [Math]::Max($src.Width, $src.Height))
	$w = [Math]::Max(1, [int]($src.Width * $scale))
	$h = [Math]::Max(1, [int]($src.Height * $scale))
	$dst = New-Object System.Drawing.Bitmap $w, $h
	$g = [System.Drawing.Graphics]::FromImage($dst)
	$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
	$g.DrawImage($src, 0, 0, $w, $h)
	$g.Dispose()
	$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
	$params = New-Object System.Drawing.Imaging.EncoderParameters 1
	$params.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), ([long]$env:HB_QUALITY)
	$dst.Save($env:HB_DST, $codec, $params)
	$dst.Dispose()
	Write-Output "$($src.Width)x$($src.Height) $($w)x$($h)"
}
finally { $src.Dispose() }
`;

function toJpeg(src, dst, { maxSize = 1568, quality = 85 } = {}) {
	return new Promise((resolve, reject) => {
		execFile('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', SCRIPT], {
			env: { ...process.env, HB_SRC: src, HB_DST: dst, HB_MAX: String(maxSize), HB_QUALITY: String(quality) },
			windowsHide: true,
			timeout: 30000,
		}, (error, stdout, stderr) => {
			if (error) {
				reject(new Error(`resizing the screenshot failed: ${stderr || error.message}`));
				return;
			}
			const m = /(\d+)x(\d+) (\d+)x(\d+)/.exec(stdout);
			resolve({
				data: fs.readFileSync(dst).toString('base64'),
				original: m ? { width: +m[1], height: +m[2] } : null,
				size: m ? { width: +m[3], height: +m[4] } : null,
			});
		});
	});
}

module.exports = { toJpeg };
