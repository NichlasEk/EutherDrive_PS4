// SPDX-License-Identifier: MIT
using EutherDrive.Core;
namespace Orbis {
    internal sealed class Emulator : IDisposable {
        private IEmulatorCore core;
        private uint[] pixels = new uint[0];
        public static int Width = 320, Height = 224;
        public int Period { get; private set; } = 16667;
        public string SystemName { get; private set; }
        public Emulator Ppu => this;
        public void LoadRom(string path) {
            Directory.CreateDirectory(Framework.LogDirectory);
            string ext = Path.GetExtension(path).ToLowerInvariant();
            if (ext == ".sfc" || ext == ".smc") {
                core = new SnesAdapter(); SystemName = "Super Nintendo";
            } else {
                core = new MdTracerAdapter(); SystemName = ext == ".sms" ? "Master System" : "Mega Drive";
            }
            core.LoadRom(path);
            double fps = core is MdTracerAdapter md ? md.GetTargetFps() : ((SnesAdapter)core).GetTargetFps(ConsoleRegion.Auto);
            Period = (int)Math.Round(1000000.0 / fps);
        }
        public void SetController(int keys) {
            // MD: X=A, O=B, Square=C, Triangle=X, L2=Y, R2=Z.
            // SNES: O=A, X=B, Triangle=X, Square=Y, L1=L, R1=R.
            bool snes = core is SnesAdapter;
            core.SetInputState((keys&0x10)!=0,(keys&0x40)!=0,(keys&0x80)!=0,(keys&0x20)!=0,
                (keys&(snes?0x2000:0x4000))!=0,(keys&(snes?0x4000:0x2000))!=0,
                (keys&(snes?0x800:0x8000))!=0,(keys&8)!=0,
                (keys&0x1000)!=0,(keys&(snes?0x8000:0x100))!=0,
                (keys&(snes?0x400:0x200))!=0,(keys&1)!=0,PadType.SixButton);
        }
        public void SetInputState(bool up,bool down,bool left,bool right,bool a,bool b,bool start,bool select) {
            core.SetInputState(up,down,left,right,a,b,false,start,false,false,false,select,PadType.SixButton);
        }
        public void RunFrame() { PerformanceProbe.BeginFrame(); core.RunFrame(); }
        public string PerformanceDetail => SystemName=="Master System" ? PerformanceProbe.Summary() : SystemName;
        public void ResetPerformance() => PerformanceProbe.Reset();
        public ReadOnlySpan<short> ConsumeAudioBuffer() {
            int rate, channels;
            var audio = core.GetAudioBuffer(out rate,out channels);
            if(rate!=44100 || channels!=2) throw new Exception("Unsupported audio format: "+rate+" / "+channels);
            return audio;
        }
        public uint[] GetFrameBuffer() {
            int w,h,stride;
            var bytes=core.GetFrameBuffer(out w,out h,out stride);
            if(w<1 || w>640 || h<1 || h>480 || stride<w*4 || bytes.Length<stride*h)
                throw new Exception("Invalid console framebuffer");
            Width=w; Height=h;
            if(pixels.Length!=w*h)pixels=new uint[w*h];
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                int j=y*stride+x*4;
                pixels[y*w+x]=(uint)(bytes[j]|bytes[j+1]<<8|bytes[j+2]<<16)|0xff000000u;
            }
            return pixels;
        }
        public string GetDebugState() => SystemName;
        public void Dispose() { if(core is IDisposable d)d.Dispose(); core=null; }
    }
}
