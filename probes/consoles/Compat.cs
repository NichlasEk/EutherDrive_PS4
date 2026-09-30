// Framework compatibility only. Emulator algorithms stay in the snapshot.
namespace Orbis {
    internal static class Framework {
        public static int Clamp(int v,int lo,int hi) => Math.Min(hi,Math.Max(lo,v));
        public static long Clamp(long v,long lo,long hi) => Math.Min(hi,Math.Max(lo,v));
        public static double Clamp(double v,double lo,double hi) => Math.Min(hi,Math.Max(lo,v));
        public static float Clamp(float v,float lo,float hi) => Math.Min(hi,Math.Max(lo,v));
        public static ulong Clamp(ulong v,ulong lo,ulong hi) => Math.Min(hi,Math.Max(lo,v));
        public static string LogDirectory {
            get {
                string path=Environment.GetEnvironmentVariable("ED_GB_CAPTURE");
                return string.IsNullOrEmpty(path) ? "/data/eutherdrive-ps4/logs" : Path.Combine(path,"logs");
            }
        }
        public static T[] Empty<T>() => new T[0];
        public static void Fill<T>(T[] a,T value) { for(int i=0;i<a.Length;++i)a[i]=value; }
        public static void Fill<T>(T[] a,T value,int start,int count) { for(int i=start;i<start+count;++i)a[i]=value; }
        public static bool IsFinite(double v) => !double.IsNaN(v) && !double.IsInfinity(v);
        public static bool IsByRefLike(Type t) => t.GetCustomAttributes(false).Any(a => a.GetType().FullName == "System.Runtime.CompilerServices.IsByRefLikeAttribute");
        public static string Create(IFormatProvider provider,string text) => text;
        public static long TickCount64 => (long)(System.Diagnostics.Stopwatch.GetTimestamp() * (1000.0 / System.Diagnostics.Stopwatch.Frequency));
        public static string ToHexString(byte[] data) => BitConverter.ToString(data).Replace("-", "");
        public static byte[] Sha1(byte[] data) { using(var sha=System.Security.Cryptography.SHA1.Create()) return sha.ComputeHash(data); }
        public static byte[] Sha1(Span<byte> data) => Sha1(data.ToArray());
        public static int TrailingZeroCount(int v) => TrailingZeroCount(unchecked((uint)v));
        public static int TrailingZeroCount(uint v) { if(v==0)return 32;int n=0;for(;(v&1)==0;v>>=1)++n;return n; }
        public static string[] Split(string s,char c,StringSplitOptions opt) => Split(s,new[]{c},int.MaxValue,opt);
        public static string[] Split(string s,char c,int count,StringSplitOptions opt) => Split(s,new[]{c},count,opt);
        public static string[] Split(string s,char[] c,StringSplitOptions opt) => Split(s,c,int.MaxValue,opt);
        public static string[] Split(string s,char[] c,int count,StringSplitOptions opt) {
            var parts=s.Split(c,count,opt & StringSplitOptions.RemoveEmptyEntries);
            return ((int)opt & 2) != 0 ? parts.Select(p=>p.Trim()).Where(p=>((int)opt&1)==0 || p.Length>0).ToArray() : parts;
        }
        public static string[] Split(string s,params char[] c) => s.Split(c);
        public static float Round(float v) => (float)Math.Round(v);
        public static int PopCount(uint v) {int n=0; for(;v!=0;v>>=1)n+=(int)(v&1);return n;}
        public static int LeadingZeroCount(uint v) {int n=0;for(uint mask=0x80000000;mask!=0&&(v&mask)==0;mask>>=1)++n;return n;}
        public static uint RotateRight(uint v,int n) => (v>>(n&31))|(v<<((-n)&31));
    }
}
// PS4 has ordinary file paths; Android virtual/document providers are not used.
namespace ProjectPSX.IO {
    public static class VirtualFileSystem {
        public static bool DirectoryExists(string path) => Directory.Exists(path);
        public static string[] GetFiles(string path,string pattern) => Directory.GetFiles(path,pattern);
        public static string[] GetFiles(string path) => Directory.GetFiles(path);
        public static bool Exists(string path) => File.Exists(path);
        public static Stream OpenRead(string path) => File.OpenRead(path);
        public static byte[] ReadAllBytes(string path) => File.ReadAllBytes(path);
        public static string ReadAllText(string path) => File.ReadAllText(path);
        public static string[] ReadAllLines(string path) => File.ReadAllLines(path);
        public static long GetLength(string path) => new FileInfo(path).Length;
    }
}
namespace Orbis {
    internal static partial class FrameworkExtraPlaceholder { }
}
namespace System {
    internal static class ProfileExtensions {
        public static int Read(this Stream stream, Span<byte> target) {
            byte[] buffer = new byte[target.Length];
            int n = stream.Read(buffer,0,buffer.Length);
            buffer.AsSpan(0,n).CopyTo(target); return n;
        }
        public static string GetString(this System.Text.Encoding encoding, ReadOnlySpan<byte> bytes) => encoding.GetString(bytes.ToArray());
        public static string GetString(this System.Text.Encoding encoding, Span<byte> bytes) => encoding.GetString(bytes.ToArray());
        public static V GetValueOrDefault<K,V>(this Dictionary<K,V> values,K key) { V v;return values.TryGetValue(key,out v)?v:default(V); }
    }
}
