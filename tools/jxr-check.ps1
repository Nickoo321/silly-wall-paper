# Decode a shot's .jxr back through WIC and report what is actually in it:
# the native pixel format, and the brightest channel in nits (scRGB 1.0 = 80
# nits). Run it against the [shot] log line -- max_scRGB / max nits there and
# "max ch" here must agree, otherwise the encoder quietly re-quantised us.
#
#   .\tools\jxr-check.ps1 build2\shots\live\jxr-acid12.jxr
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [double]$ExpectNits = -1     # the [shot] log line's "max_scRGB (N nits)"
)

$full = (Resolve-Path -LiteralPath $Path).Path

Add-Type -ReferencedAssemblies PresentationCore, WindowsBase, System.Xaml -TypeDefinition @'
using System;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

public static class JxrCheck {
    // WPF has no public name for WIC's 64bppRGBAHalf, so it prints "Default".
    // The underlying WIC GUID is on an internal property -- take it from there
    // rather than guessing from the bit depth.
    static string WicGuid(PixelFormat pf) {
        try {
            var p = typeof(PixelFormat).GetProperty("Guid",
                        System.Reflection.BindingFlags.NonPublic |
                        System.Reflection.BindingFlags.Instance);
            if (p != null) return ((Guid)p.GetValue(pf, null)).ToString();
        } catch { }
        return "(unavailable)";
    }

    public static void Run(string path, double expectNits) {
        var dec = BitmapDecoder.Create(new Uri(path),
                    BitmapCreateOptions.PreservePixelFormat,
                    BitmapCacheOption.OnLoad);
        BitmapFrame f = dec.Frames[0];
        Console.WriteLine("file     : " + path);
        Console.WriteLine("decoder  : " + dec.GetType().Name);
        Console.WriteLine("size     : " + f.PixelWidth + "x" + f.PixelHeight);
        Console.WriteLine("format   : " + f.Format + "  (" + f.Format.BitsPerPixel + " bpp)");
        Console.WriteLine("wic guid : " + WicGuid(f.Format));

        BitmapSource src = f;
        if (f.Format != PixelFormats.Rgba128Float)
            src = new FormatConvertedBitmap(f, PixelFormats.Rgba128Float, null, 0.0);

        int w = src.PixelWidth, h = src.PixelHeight;
        int stride = w * 16;
        float[] row = new float[w * 4];
        double maxv = double.NegativeInfinity, minv = double.PositiveInfinity;
        double sum = 0; long neg = 0; long cnt = 0;
        for (int y = 0; y < h; y++) {
            src.CopyPixels(new Int32Rect(0, y, w, 1), row, stride, 0);
            for (int i = 0; i < w; i++) {
                float r = row[i * 4], g = row[i * 4 + 1], b = row[i * 4 + 2];
                double m = Math.Max(r, Math.Max(g, b));
                double n = Math.Min(r, Math.Min(g, b));
                if (m > maxv) maxv = m;
                if (n < minv) minv = n;
                if (n < 0.0) neg++;
                sum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
                cnt++;
            }
        }
        Console.WriteLine("max ch   : " + maxv.ToString("F4") + " scRGB = "
                          + (maxv * 80.0).ToString("F0") + " nits");
        Console.WriteLine("min ch   : " + minv.ToString("F4") + " scRGB");
        Console.WriteLine("mean lum : " + (sum / cnt).ToString("F4") + " scRGB = "
                          + (sum / cnt * 80.0).ToString("F1") + " nits");
        Console.WriteLine("negative : " + (100.0 * neg / cnt).ToString("F2") + "%");

        // No integer encoding can hold either of these, so seeing both proves
        // the file really carries the scene-referred floats, whatever name
        // WPF prints for the format.
        bool floaty = (maxv > 1.0001) && (minv < -0.0001);
        Console.WriteLine("VERDICT  : " + (floaty
            ? "float scRGB (out-of-range highs AND wide-gamut negatives survived)"
            : "SUSPECT -- no values outside 0..1, this may have been quantised"));
        if (expectNits >= 0) {
            double got = maxv * 80.0;
            bool ok = Math.Abs(got - expectNits) <= Math.Max(1.0, expectNits * 0.002);
            Console.WriteLine("vs log   : log max " + expectNits.ToString("F0")
                + " nits, file max " + got.ToString("F0") + " nits -> "
                + (ok ? "MATCH" : "MISMATCH"));
        }
    }
}
'@

[JxrCheck]::Run($full, $ExpectNits)
