// SPDX-License-Identifier: MIT
using System.Diagnostics;
namespace Orbis {
    // Sample one complete SMS frame in 32. Never time individual instructions.
    internal static class PerformanceProbe {
        private static int frames;
        private static long cpuTicks, vdpTicks, samples;
        internal static bool Sampling;
        internal static void Reset() { frames=0; cpuTicks=vdpTicks=samples=0; Sampling=false; }
        internal static void BeginFrame() { Sampling=(++frames & 31)==0; if(Sampling)++samples; }
        internal static long Start() => Sampling ? Stopwatch.GetTimestamp() : 0;
        internal static void Cpu(long start) { if(Sampling)cpuTicks+=Stopwatch.GetTimestamp()-start; }
        internal static void Vdp(long start) { if(Sampling)vdpTicks+=Stopwatch.GetTimestamp()-start; }
        internal static string Summary() {
            if(samples==0)return "core samples pending";
            double ms=1000.0/Stopwatch.Frequency/samples;
            return string.Format("SMS Z80 {0:F1} VDP {1:F1}",cpuTicks*ms,vdpTicks*ms);
        }
    }
}
