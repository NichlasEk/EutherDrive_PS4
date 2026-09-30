// SPDX-License-Identifier: MIT
using System;
using System.Globalization;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
namespace MonoBenchmark {
    public static class Entry {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void NativeReport(IntPtr text);
        public static void Log(string format, params object[] args) {
            string line = args.Length == 0 ? format : string.Format(CultureInfo.InvariantCulture, format, args);
            IntPtr text = Marshal.StringToHGlobalAnsi(line);
            try { NativeReport(text); }
            finally { Marshal.FreeHGlobal(text); }
        }
        public static void Main() {
            try {
                Benchmark.Run(new string[] { "@ROM@", "300", "@FRAMES@" });
                File.WriteAllText("/data/eutherdrive-ps4/benchmark-result.log", "RESULT PASS\n");
                Log("RESULT PASS");
            } catch (Exception e) { Log("BENCHFAIL " + e); throw; }
        }
    }
}
