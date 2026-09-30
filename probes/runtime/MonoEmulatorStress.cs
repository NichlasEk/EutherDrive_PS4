// SPDX-License-Identifier: MIT
using System;
using System.IO;
using System.Threading;
namespace Orbis {
    internal static class MonoEmulatorStress {
        static double dividend = 7.5, divisor = 2.0;
        public static void MathAndFormatting() {
            double a = Thread.VolatileRead(ref dividend), b = Thread.VolatileRead(ref divisor);
            if (a % b != 1.5 || -a % b != -1.5) throw new Exception("floating remainder mismatch");
            if (!double.IsNaN(a % 0.0)) throw new Exception("NaN classification mismatch");
            if (a.ToString("F3", System.Globalization.CultureInfo.InvariantCulture) != "7.500")
                throw new Exception("floating formatting mismatch");
        }
        static int stop, work;
        static Exception workerFailure;
        static void Worker() {
            try {
                while (Interlocked.CompareExchange(ref stop, 0, 0) == 0) {
                    var data = new byte[8192];
                    data[0] = 42;
                    if (data[0] != 42) throw new Exception("worker data mismatch");
                    Interlocked.Increment(ref work);
                }
            } catch (Exception e) { workerFailure = e; }
        }
        public static void ConcurrentGc() {
            stop = work = 0;
            workerFailure = null;
            var a = new Thread(Worker); var b = new Thread(Worker);
            a.Start(); b.Start();
            try {
                for (int i = 0; i < 8; ++i) {
                    Thread.Sleep(5);
                    GC.Collect();
                    GC.WaitForPendingFinalizers();
                }
            } finally { Interlocked.Exchange(ref stop, 1); }
            if (!a.Join(5000) || !b.Join(5000)) throw new Exception("GC workers did not stop");
            if (workerFailure != null) throw workerFailure;
            if (work < 2) throw new Exception("workers made no progress during GC");
        }
        public static void DirectoryIo() {
            string path = "/data/eutherdrive-ps4/directory-probe";
            Directory.CreateDirectory(path);
            File.WriteAllText(path + "/first.txt", "first");
            File.WriteAllText(path + "/second.txt", "second");
            string[] files = Directory.GetFiles(path, "*.txt");
            Array.Sort(files, StringComparer.Ordinal);
            if (files.Length != 2 || Path.GetFileName(files[0]) != "first.txt" ||
                Path.GetFileName(files[1]) != "second.txt") throw new Exception("directory enumeration mismatch");
            foreach (string file in files) File.Delete(file);
            Directory.Delete(path);
            if (Directory.Exists(path)) throw new Exception("directory removal failed");
        }
    }
}
