// SPDX-License-Identifier: MIT
// Thin presentation adapter for EutherDrive's independent SMS/GG interpreter.
using EutherDrive.Core.SmsGg;
namespace Orbis {
    internal static class Framework {
        public static int Clamp(int v, int lo, int hi) => Math.Min(hi, Math.Max(lo, v));
        public static double Clamp(double v, double lo, double hi) => Math.Min(hi, Math.Max(lo, v));
        public static int PopCount(byte v) { int n = 0; for (; v != 0; v >>= 1) n += v & 1; return n; }
    }
    internal sealed class Emulator : IDisposable {
        private readonly SmsGgSeedCore core = new SmsGgSeedCore();
        private readonly SmsGgInputState input = new SmsGgInputState();
        private readonly uint[] pixels = new uint[256 * 240];
        public Emulator Ppu => this;
        public void LoadRom(string path) {
            core.LoadRom(path);
            if (core.Hardware != SmsGgHardware.MasterSystem) throw new Exception("Expected Master System ROM");
        }
        public void SetInputState(bool up, bool down, bool left, bool right, bool a, bool b, bool start, bool select) {
            input.UpdateFromEutherInput(up, down, left, right, a, b, start);
            core.SetInputState(input);
        }
        public void RunFrame() => core.RunFrame();
        public ReadOnlySpan<short> ConsumeAudioBuffer() {
            int rate, channels;
            var audio = core.GetAudioBuffer(out rate, out channels);
            if (rate != 44100 || channels != 2) throw new Exception("Unsupported audio format");
            return audio;
        }
        public uint[] GetFrameBuffer() {
            int w, h, stride;
            var bytes = core.GetFrameBuffer(out w, out h, out stride);
            if (w != 256 || h != 240 || stride != 1024) throw new Exception("Unsupported SMS frame");
            for (int i = 0; i < pixels.Length; ++i) {
                int j = i * 4;
                pixels[i] = (uint)(bytes[j] | bytes[j+1] << 8 | bytes[j+2] << 16) | 0xff000000u;
            }
            return pixels;
        }
        public string GetDebugState() => "SMS PC=" + core.ProgramCounter.ToString("x4");
        public void Dispose() { }
    }
}
namespace EutherDrive.Core.SmsGg {
    internal static class SmsGgRomLoader {
        public static (byte[] RomBytes, string DisplayName) Load(string path) =>
            (File.ReadAllBytes(path), Path.GetFileName(path));
    }
}
