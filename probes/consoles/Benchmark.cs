// SPDX-License-Identifier: MIT
// Uses the already-built player, including its actual backend and audio path.
// Shared by desktop Mono and the PS4 Mono diagnostic runner.
// GPU presentation and the native audio queue are excluded.
using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Collections.Generic;
using System.Reflection;
using EutherDrive.Core;

class Benchmark {
    delegate ReadOnlySpan<short> Audio();
    const BindingFlags Methods = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;
    static T Bind<T>(object instance, string name) where T : class {
        return Delegate.CreateDelegate(typeof(T), instance, instance.GetType().GetMethod(name, Methods)) as T;
    }
    static int[][] ReadInput(string path) {
        var result = new List<int[]>();
        foreach (string line in File.ReadAllLines(path)) {
            string text = line.Trim();
            if (text.Length == 0 || text.StartsWith("#")) continue;
            string[] parts = text.Split(new char[] {' ', '\t'}, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length != 3) throw new ArgumentException("Input row requires first-frame last-frame buttons");
            int first = int.Parse(parts[0]), last = int.Parse(parts[1]), buttons = int.Parse(parts[2]);
            if (first < 1 || last < first || buttons < 0 || buttons > 255)
                throw new ArgumentException("Invalid input range or button mask");
            result.Add(new int[] {first, last, buttons});
        }
        return result.ToArray();
    }
    // Capture after timing has finished, to identify the measured scene.
    static void SaveFrame(string path, uint[] pixels, int width, int height) {
        if (width <= 0 || height <= 0 || pixels.Length != width * height)
            throw new Exception("Invalid benchmark capture dimensions");
        byte[] header = System.Text.Encoding.ASCII.GetBytes("P6\n" + width + " " + height + "\n255\n");
        byte[] ppm = new byte[header.Length + pixels.Length * 3];
        Buffer.BlockCopy(header, 0, ppm, 0, header.Length);
        for (int i = 0, j = header.Length; i < pixels.Length; ++i) {
            ppm[j++] = (byte)(pixels[i] >> 16);
            ppm[j++] = (byte)(pixels[i] >> 8);
            ppm[j++] = (byte)pixels[i];
        }
        File.WriteAllBytes(path, ppm);
    }
    static void Main(string[] args) {
        System.Threading.Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
        int warmup = int.Parse(args[1]), count = int.Parse(args[2]);
        if (warmup < 300 || count < 1) throw new ArgumentException("Need >=300 warmup frames and positive measured frames");
        int[][] replay = args.Length > 3 && args[3].Length > 0 ? ReadInput(args[3]) : null;
        var type = typeof(MdTracerAdapter).Assembly.GetType("Orbis.Emulator", true);
        using (var emulator = (IDisposable)Activator.CreateInstance(type, true)) {
            Bind<Action<string>>(emulator, "LoadRom")(args[0]);
            var input = Bind<Action<bool,bool,bool,bool,bool,bool,bool,bool>>(emulator, "SetInputState");
            var run = Bind<Action>(emulator, "RunFrame");
            var audio = Bind<Audio>(emulator, "ConsumeAudioBuffer");
            var pixels = Bind<Func<uint[]>>(emulator, "GetFrameBuffer");
            string system = (string)type.GetProperty("SystemName").GetValue(emulator, null);
            long coreTicks=0, audioTicks=0, copyTicks=0, samples=0, nonzero=0;
            uint videoHash=2166136261, audioHash=2166136261;
            var frameTicks = new long[count];
            int[] gc = {GC.CollectionCount(0), GC.CollectionCount(1), GC.CollectionCount(2)};
            long wallStart=0;
            for (int frame=1; frame<=warmup+count; ++frame) {
                if (frame==warmup+1) {
                    Bind<Action>(emulator, "ResetPerformance")();
                    for(int g=0;g<3;++g)gc[g]=GC.CollectionCount(g);
                    wallStart=Stopwatch.GetTimestamp();
                }
                bool press=(frame>=120 && frame<=130) || (frame>=700 && frame<=710)
                    || (frame>=900 && frame<=910) || (frame>=1100 && frame<=1110);
                int buttons = press ? 16 | (system != "Master System" ? 64 : 0) : 0;
                if (replay != null) {
                    buttons = 0;
                    foreach (int[] row in replay)
                        if (frame >= row[0] && frame <= row[1]) buttons |= row[2];
                }
                input((buttons&1)!=0,(buttons&2)!=0,(buttons&4)!=0,(buttons&8)!=0,
                      (buttons&16)!=0,(buttons&32)!=0,(buttons&64)!=0,(buttons&128)!=0);
                long a=Stopwatch.GetTimestamp(); run();
                long b=Stopwatch.GetTimestamp(); var sound=audio();
                long c=Stopwatch.GetTimestamp(); var image=pixels();
                long d=Stopwatch.GetTimestamp();
                if (frame<=warmup) continue;
                coreTicks+=b-a; audioTicks+=c-b; copyTicks+=d-c;
                frameTicks[frame-warmup-1]=d-a;
                // Hash all output outside the stage timers. Wall FPS includes this cost.
                foreach(short sample in sound) {
                    ++samples; if(sample!=0)++nonzero;
                    audioHash=unchecked((audioHash^(ushort)sample)*16777619);
                }
                foreach(uint pixel in image)videoHash=unchecked((videoHash^pixel)*16777619);
            }
            double wall=(Stopwatch.GetTimestamp()-wallStart)/(double)Stopwatch.Frequency;
            double ms=1000.0/Stopwatch.Frequency/count;
            Array.Sort(frameTicks);
            Console.WriteLine("BENCH system={0} frames={1} warmup={2} pipeline_fps={3:F2} wall_fps={4:F2} core_ms={5:F3} audio_ms={6:F3} copy_ms={7:F3} p95_ms={8:F3} video_hash={9:x8} audio_hash={10:x8} samples={11} nonzero={12} gc={13}/{14}/{15}",
                system,count,warmup,1000/((coreTicks+audioTicks+copyTicks)*ms),count/wall,
                coreTicks*ms,audioTicks*ms,copyTicks*ms,
                frameTicks[(int)Math.Ceiling(count*0.95)-1]*1000.0/Stopwatch.Frequency,
                videoHash,audioHash,samples,nonzero,
                GC.CollectionCount(0)-gc[0],GC.CollectionCount(1)-gc[1],GC.CollectionCount(2)-gc[2]);
            Console.WriteLine("PROFILE "+type.GetProperty("PerformanceDetail").GetValue(emulator,null));
            if (args.Length > 4 && args[4].Length > 0)
                SaveFrame(args[4], pixels(), (int)type.GetField("Width").GetValue(null), (int)type.GetField("Height").GetValue(null));
            if(samples==0 || nonzero==0)throw new Exception("No audible samples produced");
        }
    }
}
