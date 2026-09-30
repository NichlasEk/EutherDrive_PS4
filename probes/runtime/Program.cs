// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;

namespace Orbis
{
    public static class Program
    {
        private static readonly bool HostValidation =
            Environment.GetEnvironmentVariable("EUTHERDRIVE_RUNTIME_PROBE_HOST") == "1";
        private static readonly string OutputDirectory = HostValidation
            ? Path.Combine(Path.GetTempPath(), "eutherdrive-ps4-runtime-probe")
            : "/data/eutherdrive-ps4";
        private static readonly string OutputPath =
            Path.Combine(OutputDirectory, "runtime-probe.log");
        private static readonly StringBuilder Report = new StringBuilder();
        private static int _failures;

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void NativeReport(IntPtr text);

        [MethodImpl(MethodImplOptions.NoInlining)]
        private static void SendNativeReport(string line)
        {
            IntPtr text = Marshal.StringToHGlobalAnsi(line);
            try { NativeReport(text); }
            finally { Marshal.FreeHGlobal(text); }
        }

        [DllImport("libkernel", EntryPoint = "sceKernelGetProcessTime")]
        private static extern ulong GetProcessTime();

        private sealed class Box<T>
        {
            public T Value;

            public Box(T value)
            {
                Value = value;
            }
        }

        public static void Main()
        {
            Record("EutherDrive PS4 managed runtime probe 0.06");
            Run("arrays", TestArrays);
            Run("span", TestSpan);
            Run("generics", TestGenerics);
            Run("exceptions", TestExceptions);
            Run("gc", TestGarbageCollector);
            Run("timing", TestTiming);
            Run("threads", TestThreading);
            Run("native-call", TestNativeCall);
            Run("file-io", TestFileIo);
            Record(_failures == 0 ? "RESULT PASS" : "RESULT FAIL count=" + _failures);
            PersistReport();
        }

        private static void Run(string name, Action test)
        {
            try
            {
                Record("BEGIN " + name);
                test();
                Record("PASS " + name);
            }
            catch (Exception exception)
            {
                ++_failures;
                Record("FAIL " + name + ": " + exception.GetType().FullName + ": " + exception.Message);
            }
        }

        private static void TestArrays()
        {
            var values = new int[256];
            long sum = 0;
            for (var index = 0; index < values.Length; ++index)
            {
                values[index] = index * 3 - 7;
                sum += values[index];
            }

            Require(sum == 96128, "array checksum mismatch");
        }

        private static void TestSpan()
        {
            var values = new[] { 2, 3, 5, 7, 11, 13 };
            var middle = new Span<int>(values, 1, 4);
            middle[2] = 17;
            Require(middle.Length == 4, "Span length mismatch");
            Require(values[3] == 17, "Span did not update its backing array");
        }

        private static void TestGenerics()
        {
            var words = new List<Box<string>>
            {
                new Box<string>("Euther"),
                new Box<string>("Drive")
            };
            Require(words[0].Value + words[1].Value == "EutherDrive", "generic value mismatch");
        }

        private static void TestExceptions()
        {
            var caught = false;
            try
            {
                throw new InvalidOperationException("expected-probe-exception");
            }
            catch (InvalidOperationException exception)
            {
                caught = exception.Message == "expected-probe-exception";
            }

            Require(caught, "managed exception was not caught correctly");
        }

        private static void TestGarbageCollector()
        {
            var buffers = new byte[32][];
            for (var index = 0; index < buffers.Length; ++index)
                buffers[index] = new byte[32 * 1024];

            buffers = null;
            GC.Collect();
            GC.WaitForPendingFinalizers();
            GC.Collect();
            Require(GC.GetTotalMemory(false) > 0, "GC returned an invalid heap size");
        }

        private static void TestTiming()
        {
            var stopwatch = Stopwatch.StartNew();
            long checksum = 0;
            for (var index = 0; index < 100000; ++index)
                checksum += index;
            stopwatch.Stop();

            Require(checksum == 4999950000L, "timed loop checksum mismatch");
            Require(stopwatch.ElapsedTicks >= 0, "negative elapsed time");
        }

        private static void TestThreading()
        {
            var result = 0;
            var worker = new Thread(new ThreadStart(delegate { result = 6 * 7; }));
            worker.Start();
            Require(worker.Join(5000), "worker thread timed out");
            Require(result == 42, "worker thread result mismatch");
        }

        private static void TestNativeCall()
        {
            if (HostValidation)
            {
                Record("SKIP native-call host validation");
                return;
            }

            Require(GetProcessTime() > 0, "sceKernelGetProcessTime returned zero");
        }

        private static void TestFileIo()
        {
            Directory.CreateDirectory(OutputDirectory);
            var checkPath = OutputDirectory + "/runtime-probe-io.tmp";
            const string expected = "eutherdrive-runtime-probe";
            File.WriteAllText(checkPath, expected);
            Require(File.ReadAllText(checkPath) == expected, "file round trip mismatch");
            File.Delete(checkPath);
        }

        private static void Require(bool condition, string message)
        {
            if (!condition)
                throw new InvalidOperationException(message);
        }

        private static void Record(string line)
        {
            Report.AppendLine(line);
            Console.WriteLine(line);
            if (!HostValidation)
                SendNativeReport(line);
            PersistReport();
        }

        private static void PersistReport()
        {
            try
            {
                Directory.CreateDirectory(OutputDirectory);
                File.WriteAllText(OutputPath, Report.ToString());
                Console.WriteLine("REPORT " + OutputPath);
            }
            catch (Exception exception)
            {
                Console.WriteLine("REPORT FAILED: " + exception);
            }
        }
    }
}
