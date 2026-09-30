// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Nichlas Eklöf
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
#if !SMS_PLAYER && !CONSOLE_PLAYER
using EutherDrive.Core.GbEmu;
#endif

namespace Orbis {
    public static class Program {
        private static readonly bool Host = Environment.GetEnvironmentVariable("EUTHERDRIVE_RUNTIME_PROBE_HOST") == "1";
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void NativeReport(IntPtr text);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int NativeInput();
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int NativePresent(IntPtr pixels, int count);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void NativeClose();
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int NativeMenu(IntPtr text);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int NativeAudio(IntPtr samples, int count);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void NativeAudioStop();
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern void NativeMute(int mute);
        #if CONSOLE_PLAYER
        private static int Width => Emulator.Width;
        private static int Height => Emulator.Height;
        private const string SystemName = "Master System / Mega Drive / SNES", Version = "0.13";
        private static bool Supports(string ext) => new[] { ".sms", ".md", ".gen", ".smd", ".sfc", ".smc" }.Contains(ext);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int NativePreview(IntPtr pixels, int count, int width, int height);
        [MethodImpl(MethodImplOptions.InternalCall)] private static extern int NativePresentFrame(IntPtr pixels, int count, int width, int height, int period);
#elif SMS_PLAYER
        private const int Width = 256, Height = 240;
        private const string SystemName = "Master System", Version = "0.10";
        private static bool Supports(string ext) => ext == ".sms";
#else
        private const int Width = 160, Height = 144;
        private const string SystemName = "Game Boy / Color", Version = "0.09";
        private static bool Supports(string ext) => ext == ".gb" || ext == ".gbc";
#endif
        private static bool Muted;
        private static bool AudioFailed;
        private static readonly short[] Audio = new short[16384];
        private sealed class Game { public string Name, Path; }
        [MethodImpl(MethodImplOptions.NoInlining)]
        private static void ReportNative(string text) {
            IntPtr p = Marshal.StringToHGlobalAnsi(text);
            try { NativeReport(p); } finally { Marshal.FreeHGlobal(p); }
        }
        private static void Report(string text) {
            Console.WriteLine(text);
            if (!Host) ReportNative(text);
        }
        [MethodImpl(MethodImplOptions.NoInlining)]
        private static void Play(Emulator emulator) {
            int previous = NativeInput();
            AudioFailed = false;
            while (true) {
                int keys = NativeInput();
                if (keys < 0) throw new Exception("Controller read failed");
                if ((keys & 0xc00) == 0xc00) break;
                #if !CONSOLE_PLAYER
                if ((keys & ~previous & 0x1000) != 0) ToggleMute();
#endif
                previous = keys;
#if CONSOLE_PLAYER
                emulator.SetController(keys);
#else
                emulator.SetInputState((keys & 0x10) != 0, (keys & 0x40) != 0,
                    (keys & 0x80) != 0, (keys & 0x20) != 0, (keys & 0x4000) != 0,
                    (keys & 0x2000) != 0, (keys & 8) != 0, (keys & 0x8000) != 0);
#endif
                emulator.RunFrame();
                var audio = emulator.ConsumeAudioBuffer();
                if (!AudioFailed && audio.Length > 0) {
                    if (audio.Length > Audio.Length) throw new Exception("Audio chunk too large");
                    audio.CopyTo(Audio.AsSpan());
                    var audioPin = GCHandle.Alloc(Audio, GCHandleType.Pinned);
                    try {
                        if (NativeAudio(audioPin.AddrOfPinnedObject(), audio.Length) == 0) {
                            AudioFailed = true; NativeAudioStop(); NativeMute(1);
                            Report("WARN audio unavailable; gameplay continues muted");
                        }
                    } finally { audioPin.Free(); }
                }
                uint[] pixels = emulator.Ppu.GetFrameBuffer();
                var pin = GCHandle.Alloc(pixels, GCHandleType.Pinned);
                try {
                    #if CONSOLE_PLAYER
                    if (NativePresentFrame(pin.AddrOfPinnedObject(), pixels.Length, Width, Height, emulator.Period) == 0)
#else
                    if (NativePresent(pin.AddrOfPinnedObject(), pixels.Length) == 0)
#endif
                        throw new Exception("Video presentation failed");
                } finally { pin.Free(); }
            }
        }
        [MethodImpl(MethodImplOptions.NoInlining)]
        private static void ToggleMute() { Muted = !Muted; NativeMute(Muted || AudioFailed ? 1 : 0); }
        private static List<Game> Library() {
            var games = new List<Game>();
            foreach (string line in File.ReadAllLines("/app0/library.tsv")) {
                string[] fields = line.Split('\t');
                if (fields.Length == 2 && !fields[1].Contains("/") && !fields[1].Contains(".."))
                    games.Add(new Game { Name = fields[0], Path = "/app0/" + fields[1] });
            }
            foreach (string dir in new[] { "/data/eutherdrive-ps4/roms", "/mnt/usb0/EutherDrive", "/mnt/usb1/EutherDrive" }) {
                try {
                    if (!Directory.Exists(dir)) continue;
                    foreach (string path in Directory.GetFiles(dir).OrderBy(p => p)) {
                        string ext = Path.GetExtension(path).ToLowerInvariant();
                        if (Supports(ext) && games.Count < 64)
                            games.Add(new Game { Name = Path.GetFileNameWithoutExtension(path), Path = path });
                    }
                } catch (Exception error) { Report("WARN library scan: " + error.Message); }
            }
            if (games.Count == 0) throw new Exception("No ROMs found");
            return games;
        }
        private static string Label(string text) {
            return new string(text.Take(62).Select(c => c >= 32 && c <= 126 ? c : '?').ToArray());
        }
#if CONSOLE_PLAYER
        [MethodImpl(MethodImplOptions.NoInlining)]
        private static void ShowError(Exception error) {
            NativePreview(IntPtr.Zero,0,0,0);
            string message = error.GetType().Name + ": " + error.Message;
            var lines = new List<string>();
            foreach (string line in message.Replace("\r", "").Split('\n')) {
                for (int i=0; i<line.Length; i+=62)
                    lines.Add(Label(line.Substring(i,Math.Min(62,line.Length-i))));
            }
            int page=0, pages=Math.Max(1,(lines.Count+9)/10), previous=NativeInput();
            while(true) {
                int keys=NativeInput();
                if(keys<0) throw new Exception("Controller read failed");
                int pressed=keys & ~previous; previous=keys;
                if((pressed & 0x2000)!=0) return;
                if((pressed & 0x4000)!=0) page=(page+1)%pages;
                string menu="GAME ERROR " + Version + " / " + (page+1) + "/" + pages + "\nX: next page   O: return to library\n";
                menu+=string.Join("\n",lines.Skip(page*10).Take(10));
                IntPtr text=Marshal.StringToHGlobalAnsi(menu);
                try { if(NativeMenu(text)==0) throw new Exception("Error presentation failed"); }
                finally { Marshal.FreeHGlobal(text); }
            }
        }
#endif
#if CONSOLE_PLAYER
        private static void Preview(Game game) {
            NativePreview(IntPtr.Zero,0,0,0);
            string path=System.IO.Path.ChangeExtension(game.Path,".preview");
            if (!File.Exists(path)) return;
            try {
                using(var reader=new BinaryReader(File.OpenRead(path))) {
                    int w=reader.ReadInt32(), h=reader.ReadInt32();
                    if(w<1 || w>320 || h<1 || h>240 || reader.BaseStream.Length!=8L+4L*w*h) return;
                    var pixels=new uint[w*h];
                    for(int i=0;i<pixels.Length;++i)pixels[i]=reader.ReadUInt32();
                    var pin=GCHandle.Alloc(pixels,GCHandleType.Pinned);
                    try { NativePreview(pin.AddrOfPinnedObject(),pixels.Length,w,h); }
                    finally { pin.Free(); }
                }
            } catch(Exception error) { Report("WARN preview: "+error.Message); }
        }
#endif
        [MethodImpl(MethodImplOptions.NoInlining)]
        private static void Frontend() {
            var games = Library();
            int selected = 0, previous = NativeInput();
#if CONSOLE_PLAYER
            int previewSelection = -1;
#endif
            string status = "Choose a game. No battery saves yet.";
            while (true) {
                int keys = NativeInput();
                if (keys < 0) throw new Exception("Controller read failed");
                int pressed = keys & ~previous;
                previous = keys;
                if ((pressed & 0x2000) != 0) return;
                if ((pressed & 0x10) != 0) selected = (selected + games.Count - 1) % games.Count;
                if ((pressed & 0x40) != 0) selected = (selected + 1) % games.Count;
                if ((pressed & 0x1000) != 0) ToggleMute();
                if ((pressed & 0x4000) != 0) {
                    try {
                        Report("LOAD " + games[selected].Path);
                        using (var emulator = new Emulator()) {
                            emulator.LoadRom(games[selected].Path);
                            NativeMute(Muted ? 1 : 0);
                            Play(emulator);
                        }
                        status = "Returned to library. Game restarts when selected.";
                    } catch (Exception error) {
                        status = "Game stopped: " + error.Message;
                        Report("WARN game stopped: " + error);
#if CONSOLE_PLAYER
                        NativeAudioStop();
                        ShowError(error);
                        previewSelection=-1;
#endif
                    } finally { NativeAudioStop(); }
                    previous = NativeInput();
                }
#if CONSOLE_PLAYER
                if(previewSelection!=selected) { Preview(games[selected]); previewSelection=selected; }
#endif
                string menu = SystemName + "  /  " + games.Count + " games\n\n";
                int start = selected / 8 * 8;
                for (int i = start; i < Math.Min(start + 8, games.Count); ++i)
                    menu += (i == selected ? "> " : "  ") + Label(games[i].Name).Substring(0,Math.Min(42,Label(games[i].Name).Length)) + "\n";
                menu += "\n" + Label(status);
                IntPtr text = Marshal.StringToHGlobalAnsi(menu);
                try { if (NativeMenu(text) == 0) throw new Exception("Menu presentation failed"); }
                finally { Marshal.FreeHGlobal(text); }
            }
        }
        [MethodImpl(MethodImplOptions.NoInlining)] private static void CloseNative() { NativeClose(); }
        private static string RomPath() {
            if (Host) return Environment.GetEnvironmentVariable("ED_GB_ROM") ?? throw new Exception("ED_GB_ROM missing");
            // The packaged user-supplied ROM needs no external mount rights.
            // USB paths are best-effort; custom local ROM takes precedence.
            foreach (string path in new[] { "/data/eutherdrive-ps4/game.gb", "/data/eutherdrive-ps4/game.gbc",
                    "/mnt/usb0/EutherDrive/game.gb", "/mnt/usb1/EutherDrive/game.gb" })
                if (File.Exists(path)) return path;
            return "/app0/game.gb";
        }
        private static void Smoke(Emulator emulator) {
            uint first = 0, last = 0;
            long audioSamples = 0, nonzero = 0;
            int peak = 0;
            string folder = Environment.GetEnvironmentVariable("ED_GB_CAPTURE");
            using (var sound = new BinaryWriter(File.Create(Path.Combine(folder, "audio.wav")))) {
            sound.Write(new byte[44]);
            var colors = new HashSet<uint>();
            for (int frame = 1; frame <= 1200; ++frame) {
#if CONSOLE_PLAYER
                bool press = (frame >= 120 && frame <= 130) || (frame >= 700 && frame <= 710) || (frame >= 900 && frame <= 910) || (frame >= 1100 && frame <= 1110);
                emulator.SetInputState(false, false, false, false, press, false,
                    emulator.SystemName != "Master System" && press, false);
#else
                emulator.SetInputState(false, false, false, false, frame >= 900 && frame <= 910, false,
                    (frame >= 120 && frame <= 130) || (frame >= 700 && frame <= 710), false);
#endif
                emulator.RunFrame();
                var samples = emulator.ConsumeAudioBuffer();
                if ((samples.Length & 1) != 0) throw new Exception("Odd stereo sample count");
                for (int sampleIndex = 0; sampleIndex < samples.Length; ++sampleIndex) {
                    short sample = samples[sampleIndex];
                    ++audioSamples;
                    if (sample != 0) ++nonzero;
                    peak = Math.Max(peak, Math.Abs((int)sample));
                    sound.Write(sample);
                }
                uint hash = 2166136261;
                foreach (uint pixel in emulator.Ppu.GetFrameBuffer()) {
                    hash = unchecked((hash ^ pixel) * 16777619);
                    colors.Add(pixel);
                }
                if (frame == 100) { first = hash; SavePpm(emulator, "title.ppm"); }
                if (frame == 250) SavePpm(emulator, "game.ppm");
                if (frame == 1200) { last = hash; SavePpm(emulator, "final.ppm"); }
            }
            int size = checked((int)audioSamples * 2);
            sound.BaseStream.Position = 0;
            sound.Write(System.Text.Encoding.ASCII.GetBytes("RIFF")); sound.Write(size + 36);
            sound.Write(System.Text.Encoding.ASCII.GetBytes("WAVEfmt ")); sound.Write(16);
            sound.Write((short)1); sound.Write((short)2); sound.Write(44100); sound.Write(44100 * 4);
            sound.Write((short)4); sound.Write((short)16);
            sound.Write(System.Text.Encoding.ASCII.GetBytes("data")); sound.Write(size);
            if (nonzero == 0 || audioSamples < 1700000 || audioSamples > 1850000)
                throw new Exception("Audio silent or wrong rate: " + audioSamples + " nonzero=" + nonzero);
            Report("PASS audio samples=" + audioSamples + " nonzero=" + nonzero + " peak=" + peak);
            if (colors.Count < 2 || first == last) throw new Exception("ROM produced blank or unchanged output: colors=" + colors.Count + " " + emulator.GetDebugState());
            Report("PASS ROM 1200 frames, scripted Start/A; colors=" + colors.Count + " title=" + first.ToString("x8") + " final=" + last.ToString("x8"));
            }
        }
        private static void SavePpm(Emulator emulator, string name) {
            string folder = Environment.GetEnvironmentVariable("ED_GB_CAPTURE");
            if (string.IsNullOrEmpty(folder)) return;
            using (var file = File.Create(Path.Combine(folder, name))) {
                byte[] header = System.Text.Encoding.ASCII.GetBytes("P6\n" + Width + " " + Height + "\n255\n");
                file.Write(header, 0, header.Length);
                foreach (uint color in emulator.Ppu.GetFrameBuffer()) {
                    file.WriteByte((byte)(color >> 16)); file.WriteByte((byte)(color >> 8)); file.WriteByte((byte)color);
                }
            }
        }
        public static void Main() {
            try {
                if (Host) {
                    string path = RomPath();
                    Thread.CurrentThread.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
                    Report("EutherDrive " + SystemName + " " + Version + " ROM: " + path);
                    using (var emulator = new Emulator()) { emulator.LoadRom(path); Smoke(emulator); }
                } else Frontend();
                Report("RESULT PASS");
            } catch (Exception error) { Report("FAIL GB player " + error); }
            finally { if (!Host) CloseNative(); }
        }
    }
}
