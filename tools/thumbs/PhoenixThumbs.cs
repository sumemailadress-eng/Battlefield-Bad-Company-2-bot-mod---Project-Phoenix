// Project Phoenix: map thumbnails from the player's own Bad Company 2 install.
// Each level's overhead map (Levels/<map>/UI/minimapTexture.res, a 2048 x 2048 DXT1 texture) is read out of the game's
// .fbrb archives and saved as a small PNG (<out>\<MAP>.png, e.g. MP_001.png, NAM_MP_002.png). Nothing is downloaded and
// nothing of the game is shipped with Phoenix: the pictures are made on the player's PC from the player's game files.
// .fbrb: "FbRB", big-endian header size, gzip(header), gzip(payload). Header: version, string table, entries of 6
// big-endian uint32 (name offset, flags, payload offset, size, size, type offset). ITexture .res: 92-byte header
// (version 3, type, format 0 = DXT1, ..., width, height ...) then mip 0.
// C# 5 (Windows PowerShell 5.1's Add-Type) - no newer syntax.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;

public static class PhoenixThumbs
{
    public static string Status = "";
    public static int Made = 0;
    public static volatile bool Busy = false;

    // in the background (the launcher keeps working; it checks Busy / Status)
    public static void Start(string[] roots, string outDir, int size)
    {
        if (Busy) return;
        Busy = true; Status = "looking for the map pictures in the game files";
        System.Threading.Thread t = new System.Threading.Thread(delegate() {
            try { Run(roots, outDir, size); } catch (Exception ex) { Status = "failed: " + ex.Message; } finally { Busy = false; }
        });
        t.IsBackground = true; t.Priority = System.Threading.ThreadPriority.BelowNormal; t.Start();
    }

    static uint BE(byte[] b, int o) { return (uint)(b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3]); }
    static int LE(byte[] b, int o) { return b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24; }

    // "Levels/MP_001/UI/minimapTexture.res" -> "MP_001"; "Levels/NAM_MP_002R/UI/..." -> "NAM_MP_002"
    public static string BaseOf(string level)
    {
        string s = level.ToUpperInvariant();
        if (s.StartsWith("LEVELS/")) s = s.Substring(7);
        int sl = s.IndexOf('/'); if (sl >= 0) s = s.Substring(0, sl);
        string[] tails = { "_SDM", "_CQ", "_GR", "_SR", "SDM", "CQ", "GR", "SR" };
        foreach (string t in tails) if (s.EndsWith(t) && s.Length > t.Length + 2) { s = s.Substring(0, s.Length - t.Length); break; }
        if (s.StartsWith("NAM_") && s.EndsWith("R") && s.Length > 5 && char.IsDigit(s[s.Length - 2])) s = s.Substring(0, s.Length - 1);
        return s;
    }

    static byte[] ReadAll(Stream s) { MemoryStream m = new MemoryStream(); s.CopyTo(m); return m.ToArray(); }

    class Entry { public string Name; public uint Off; public uint Size; }

    static List<Entry> Toc(byte[] head)
    {
        List<Entry> list = new List<Entry>();
        uint strLen = BE(head, 4); int p = 8 + (int)strLen; uint n = BE(head, p); p += 4;
        for (uint i = 0; i < n && p + 24 <= head.Length; i++, p += 24) {
            uint no = BE(head, p), off = BE(head, p + 8), size = BE(head, p + 12);
            int e = 8 + (int)no, z = e; while (z < 8 + strLen && head[z] != 0) z++;
            Entry en = new Entry(); en.Name = Encoding.ASCII.GetString(head, e, z - e); en.Off = off; en.Size = size; list.Add(en);
        }
        return list;
    }

    // every fbrb under the roots (the newest data first: Package before Dist); one PNG per map
    public static int Run(string[] roots, string outDir, int size)
    {
        Made = 0; Directory.CreateDirectory(outDir);
        HashSet<string> done = new HashSet<string>();
        foreach (string f in Directory.GetFiles(outDir, "*.png")) done.Add(Path.GetFileNameWithoutExtension(f).ToUpperInvariant());
        foreach (string root in roots) {
            if (string.IsNullOrEmpty(root) || !Directory.Exists(root)) continue;
            string[] files = Directory.GetFiles(root, "*.fbrb", SearchOption.AllDirectories);
            Array.Sort(files, delegate(string a, string b) {   // (the small per-level ingame archives first)
                int ra = Path.GetFileName(a).StartsWith("ingame") ? 0 : 1, rb = Path.GetFileName(b).StartsWith("ingame") ? 0 : 1;
                return ra != rb ? ra - rb : string.Compare(a, b, StringComparison.OrdinalIgnoreCase); });
            foreach (string f in files) {
                try { Scan(f, outDir, size, done); } catch (Exception ex) { Status = Path.GetFileName(f) + ": " + ex.Message; }
            }
        }
        Status = "done: " + Made + " new thumbnail(s), " + done.Count + " in all";
        return Made;
    }

    static void Scan(string file, string outDir, int size, HashSet<string> done)
    {
        List<Entry> want = new List<Entry>();
        byte[] head;
        using (FileStream fs = File.OpenRead(file)) {
            byte[] h8 = new byte[8]; if (fs.Read(h8, 0, 8) != 8 || h8[0] != 'F' || h8[1] != 'b' || h8[2] != 'R' || h8[3] != 'B') return;
            uint hs = BE(h8, 4); if (hs == 0 || hs > 64 * 1024 * 1024) return;
            byte[] hz = new byte[hs]; if (fs.Read(hz, 0, (int)hs) != hs) return;
            using (GZipStream g = new GZipStream(new MemoryStream(hz), CompressionMode.Decompress)) head = ReadAll(g);
            foreach (Entry e in Toc(head)) {
                if (e.Size < 92 || !e.Name.EndsWith("/UI/minimapTexture.res", StringComparison.OrdinalIgnoreCase)) continue;
                string b = BaseOf(e.Name); if (done.Contains(b)) continue;
                want.Add(e);
            }
            if (want.Count == 0) return;
            want.Sort(delegate(Entry a, Entry c) { return a.Off.CompareTo(c.Off); });
            Status = "reading " + Path.GetFileName(Path.GetDirectoryName(file)) + "\\" + Path.GetFileName(file);
            using (GZipStream g = new GZipStream(fs, CompressionMode.Decompress)) {
                long pos = 0; byte[] skip = new byte[1 << 16];
                foreach (Entry e in want) {
                    while (pos < e.Off) { int r = g.Read(skip, 0, (int)Math.Min(skip.Length, e.Off - pos)); if (r <= 0) return; pos += r; }
                    if (pos > e.Off) continue;
                    byte[] d = new byte[e.Size]; int got = 0;
                    while (got < d.Length) { int r = g.Read(d, got, d.Length - got); if (r <= 0) break; got += r; }
                    pos += got; if (got < d.Length) return;
                    string b = BaseOf(e.Name); if (done.Contains(b)) continue;
                    byte[] png = Thumb(d, size);
                    if (png != null) { File.WriteAllBytes(Path.Combine(outDir, b + ".png"), png); done.Add(b); Made++; }
                }
            }
        }
    }

    // ITexture -> RGB thumbnail (box filter); DXT1 (format 0) and 32-bit (format 9)
    public static byte[] Thumb(byte[] res, int size)
    {
        int fmt = LE(res, 8), w = LE(res, 16), h = LE(res, 20);
        if (w < 4 || h < 4 || w > 8192 || h > 8192) return null;
        byte[] rgb = new byte[w * h * 3];
        if (fmt == 0) {
            if (92 + (long)w * h / 2 > res.Length) return null;
            int bw = w / 4, bh = h / 4; int[] pr = new int[4], pg = new int[4], pb = new int[4];
            for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) {
                int o = 92 + (by * bw + bx) * 8;
                int c0 = res[o] | res[o + 1] << 8, c1 = res[o + 2] | res[o + 3] << 8;
                pr[0] = (c0 >> 11 & 31) * 255 / 31; pg[0] = (c0 >> 5 & 63) * 255 / 63; pb[0] = (c0 & 31) * 255 / 31;
                pr[1] = (c1 >> 11 & 31) * 255 / 31; pg[1] = (c1 >> 5 & 63) * 255 / 63; pb[1] = (c1 & 31) * 255 / 31;
                if (c0 > c1) { pr[2] = (2 * pr[0] + pr[1]) / 3; pg[2] = (2 * pg[0] + pg[1]) / 3; pb[2] = (2 * pb[0] + pb[1]) / 3; pr[3] = (pr[0] + 2 * pr[1]) / 3; pg[3] = (pg[0] + 2 * pg[1]) / 3; pb[3] = (pb[0] + 2 * pb[1]) / 3; }
                else { pr[2] = (pr[0] + pr[1]) / 2; pg[2] = (pg[0] + pg[1]) / 2; pb[2] = (pb[0] + pb[1]) / 2; pr[3] = pg[3] = pb[3] = 0; }
                uint bits = (uint)(res[o + 4] | res[o + 5] << 8 | res[o + 6] << 16 | res[o + 7] << 24);
                for (int k = 0; k < 16; k++) { int s = (int)(bits >> (2 * k) & 3), x = bx * 4 + (k & 3), y = by * 4 + (k >> 2), q = (y * w + x) * 3; rgb[q] = (byte)pr[s]; rgb[q + 1] = (byte)pg[s]; rgb[q + 2] = (byte)pb[s]; }
            }
        } else if (fmt == 9) {
            if (92 + (long)w * h * 4 > res.Length) return null;
            for (int i = 0; i < w * h; i++) { rgb[i * 3] = res[92 + i * 4 + 2]; rgb[i * 3 + 1] = res[92 + i * 4 + 1]; rgb[i * 3 + 2] = res[92 + i * 4]; }
        } else return null;
        int tw = Math.Min(size, w), th = Math.Max(1, h * tw / w);
        byte[] t = new byte[tw * th * 3];
        for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++) {
            int x0 = x * w / tw, x1 = Math.Max(x0 + 1, (x + 1) * w / tw), y0 = y * h / th, y1 = Math.Max(y0 + 1, (y + 1) * h / th);
            long r = 0, gg = 0, bb = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { int q = (yy * w + xx) * 3; r += rgb[q]; gg += rgb[q + 1]; bb += rgb[q + 2]; n++; }
            int o = (y * tw + x) * 3; t[o] = (byte)(r / n); t[o + 1] = (byte)(gg / n); t[o + 2] = (byte)(bb / n);
        }
        return Png(t, tw, th);
    }

    // a map the game files have no overview for: drawn from Phoenix's own scan of it (<map>.nav) - the top surface of each
    // cell, shaded by slope, olive by height, water dark blue-grey, north up
    public static bool NavThumb(string navPath, string outPng, int size)
    {
        byte[] f = File.ReadAllBytes(navPath);
        if (f.Length < 60) return false;
        int w = LE(f, 48), h = LE(f, 52), L = LE(f, 56);
        if (w < 8 || h < 8 || L < 1 || L > 16 || 60L + (long)w * h * L * 5 > f.Length) return false;
        int n = w * h; float[] top = new float[n]; byte[] kind = new byte[n];   // 0 none, 1 ground, 2 water
        float lo = float.MaxValue, hi = float.MinValue;
        for (int c = 0; c < n; c++) {
            top[c] = float.NaN;
            for (int l = 0; l < L; l++) {
                int node = c * L + l; byte fl = f[60 + (long)n * L * 4 + node];
                if ((fl & 3) == 0) continue;
                float y = BitConverter.ToSingle(f, 60 + node * 4);
                if (float.IsNaN(y) || float.IsInfinity(y)) continue;
                if (float.IsNaN(top[c]) || y > top[c]) { top[c] = y; kind[c] = (byte)((fl & 2) != 0 && (fl & 1) == 0 ? 2 : 1); }
            }
            if (kind[c] == 1) { if (top[c] < lo) lo = top[c]; if (top[c] > hi) hi = top[c]; }
        }
        if (lo > hi) return false;
        int side = Math.Max(w, h);
        int tw = size, th = size;
        byte[] t = new byte[tw * th * 3];
        for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++) {
            // square picture, the grid centred in it; row 0 = north (+z)
            int cx = x * side / tw - (side - w) / 2, cz = (th - 1 - y) * side / th - (side - h) / 2;
            int o = (y * tw + x) * 3;
            if (cx < 0 || cz < 0 || cx >= w || cz >= h) { t[o] = 20; t[o + 1] = 21; t[o + 2] = 14; continue; }
            int c = cz * w + cx;
            if (kind[c] == 0) { t[o] = 24; t[o + 1] = 26; t[o + 2] = 17; continue; }
            if (kind[c] == 2) { t[o] = 38; t[o + 1] = 52; t[o + 2] = 58; continue; }
            float y0 = top[c];
            float ex = cx + 1 < w && kind[c + 1] == 1 ? top[c + 1] : y0, nz = cz + 1 < h && kind[c + w] == 1 ? top[c + w] : y0;
            float shade = 0.8f + (y0 - ex) * 0.22f + (nz - y0) * 0.22f; if (shade < 0.3f) shade = 0.3f; if (shade > 1.4f) shade = 1.4f;
            float k = (float)Math.Sqrt((y0 - lo) / Math.Max(1f, hi - lo));
            float r = 60 + 150 * k, g = 72 + 130 * k, b = 38 + 80 * k;
            t[o] = (byte)Math.Min(255, r * shade); t[o + 1] = (byte)Math.Min(255, g * shade); t[o + 2] = (byte)Math.Min(255, b * shade);
        }
        File.WriteAllBytes(outPng, Png(t, tw, th));
        return true;
    }

    // a plain RGB PNG (zlib = 2-byte header + deflate + adler32; CRC32 per chunk) - no System.Drawing needed
    static uint[] crcTable;
    static uint Crc(byte[] b, int o, int n, uint c)
    {
        if (crcTable == null) { crcTable = new uint[256]; for (uint i = 0; i < 256; i++) { uint v = i; for (int k = 0; k < 8; k++) v = (v & 1) != 0 ? 0xEDB88320u ^ (v >> 1) : v >> 1; crcTable[i] = v; } }
        for (int i = 0; i < n; i++) c = crcTable[(c ^ b[o + i]) & 0xff] ^ (c >> 8);
        return c;
    }
    static void Chunk(MemoryStream m, string type, byte[] data)
    {
        byte[] t = Encoding.ASCII.GetBytes(type); byte[] len = { (byte)(data.Length >> 24), (byte)(data.Length >> 16), (byte)(data.Length >> 8), (byte)data.Length };
        m.Write(len, 0, 4); m.Write(t, 0, 4); m.Write(data, 0, data.Length);
        uint c = Crc(data, 0, data.Length, Crc(t, 0, 4, 0xffffffffu)) ^ 0xffffffffu;
        m.Write(new byte[] { (byte)(c >> 24), (byte)(c >> 16), (byte)(c >> 8), (byte)c }, 0, 4);
    }
    public static byte[] Png(byte[] rgb, int w, int h)
    {
        byte[] raw = new byte[(w * 3 + 1) * h];
        for (int y = 0; y < h; y++) { raw[y * (w * 3 + 1)] = 0; Buffer.BlockCopy(rgb, y * w * 3, raw, y * (w * 3 + 1) + 1, w * 3); }
        MemoryStream z = new MemoryStream(); z.WriteByte(0x78); z.WriteByte(0x9c);
        using (DeflateStream d = new DeflateStream(z, CompressionMode.Compress, true)) d.Write(raw, 0, raw.Length);
        uint a = 1, b2 = 0; foreach (byte x in raw) { a = (a + x) % 65521; b2 = (b2 + a) % 65521; } uint ad = b2 << 16 | a;
        z.Write(new byte[] { (byte)(ad >> 24), (byte)(ad >> 16), (byte)(ad >> 8), (byte)ad }, 0, 4);
        MemoryStream m = new MemoryStream();
        m.Write(new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 }, 0, 8);
        byte[] ih = { (byte)(w >> 24), (byte)(w >> 16), (byte)(w >> 8), (byte)w, (byte)(h >> 24), (byte)(h >> 16), (byte)(h >> 8), (byte)h, 8, 2, 0, 0, 0 };
        Chunk(m, "IHDR", ih); Chunk(m, "IDAT", z.ToArray()); Chunk(m, "IEND", new byte[0]);
        return m.ToArray();
    }
}
