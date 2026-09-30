// SPDX-License-Identifier: MIT
// Uses the already-built player, including its actual backend and audio path.
// Desktop Mono throughput only: no PS4 runtime, GPU presentation or audio queue.
using System;
using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using EutherDrive.Core;

class Benchmark {
    delegate ReadOnlySpan<short> Audio();
    const BindingFlags Methods = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;
    static T Bind<T>(object instance, string name) where T : class {
        return Delegate.CreateDelegate(typeof(T), instance, instance.GetType().GetMethod(name, Methods)) as T;
    }
    static void Main(string[] args) {
        System.Threading.Thread.CurrentThread.CurrentCulture = CultureInfo.InvariantCulture;
        int warmup = int.Parse(args[1]), count = int.Parse(args[2]);
        if (warmup < 300 || count < 1) throw new ArgumentException("Need >=300 warmup frames and positive measured frames");
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
                input(false,false,false,false,press,false,system!="Master System" && press,false);
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
            if(samples==0 || nonzero==0)throw new Exception("No audible samples produced");
        }
    }
}
