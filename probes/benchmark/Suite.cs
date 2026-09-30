// SPDX-License-Identifier: MIT
using System;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Globalization;
namespace MonoBenchmark {
    public sealed class CaseRunner : MarshalByRefObject {
        public string Execute(string root,string output,string stem,string[] job,bool desktop) {
            Entry.Configure(output,stem,desktop);
            Benchmark.Run(new[]{Path.Combine(root,job[1]),job[3],"300",Path.Combine(root,job[2]),Path.Combine(output,stem+".ppm")});
            return Entry.LastBench;
        }
    }
    public static class IsolatedCase {
        public static void Main() {
            string[] config=Marshal.PtrToStringAnsi(Entry.NativeCaseConfig()).Split('\t');
            string[] job=new string[10];Array.Copy(config,3,job,0,10);
            try { new CaseRunner().Execute(config[0],config[1],config[2],job,false); }
            catch(Exception e) { Entry.Record("CASE ERROR isolated execution: {0}",e);throw; }
        }
    }
    public static class Entry {
        [MethodImpl(MethodImplOptions.InternalCall)] static extern void NativeReport(IntPtr message);
        [MethodImpl(MethodImplOptions.InternalCall)] static extern int NativeExport(IntPtr directory);
        [MethodImpl(MethodImplOptions.InternalCall)] static extern int NativeRunCase(IntPtr config);
        [MethodImpl(MethodImplOptions.InternalCall)] public static extern IntPtr NativeCaseConfig();
        static bool desktop;
        static string summary,current,output;
        public static string LastBench;
        public static void Configure(string directory,string stem,bool isDesktop) {
            desktop=isDesktop; output=directory;
            summary=Path.Combine(output,"summary.log");
            current=stem==null?null:Path.Combine(output,stem+".log");
            System.Threading.Thread.CurrentThread.CurrentCulture=CultureInfo.InvariantCulture;
        }
        public static void Note(string text) {
            if(summary!=null) File.AppendAllText(summary,text+"\n");
            if(desktop) { Console.WriteLine(text);return; }
            IntPtr p=Marshal.StringToHGlobalAnsi(text);
            try { NativeReport(p); } finally { Marshal.FreeHGlobal(p); }
        }
        // All reporting/captures are outside measured work. No persistent file handles.
        public static void Record(string format,params object[] args) {
            string text=string.Format(CultureInfo.InvariantCulture,format,args);
            if(current!=null) File.AppendAllText(current,text+"\n");
            if(text.StartsWith("BENCH system=")) LastBench=text;
            Note(text);
        }
        static string Hash(string path) {
            using(var sha=SHA256.Create()) using(var file=File.OpenRead(path))
                return BitConverter.ToString(sha.ComputeHash(file)).Replace("-","").ToLowerInvariant();
        }
        static void Export() {
            if(desktop)return;
            IntPtr p=Marshal.StringToHGlobalAnsi(output);
            int rc;
            try { rc=NativeExport(p); } finally { Marshal.FreeHGlobal(p); }
            Note(rc>0?"USB snapshot saved":rc==0?"USB unavailable; internal results retained for FTP":"USB export incomplete; internal results retained for FTP");
        }
        public static void Main() {
            try { Run("/app0","/data/eutherdrive-bench",false); }
            catch(Exception e) { summary=null;current=null;Note("SUITE ERROR "+e);Note("RESULT FAIL");throw; }
        }
        public static void Run(string root,string destination,bool isDesktop) {
            desktop=isDesktop;
            Directory.CreateDirectory(destination);
            int run=1;
            do { output=Path.Combine(destination,"run-"+run.ToString("D4"));++run; } while(Directory.Exists(output));
            Directory.CreateDirectory(output);
            Configure(output,null,desktop);
            int errors=0,differences=0;
            Note("EutherDrive automated benchmark 0.01; core/audio generation/framebuffer only");
            Note("OUTPUT "+output);
            Note("No timed GPU presentation or native audio output. 300 measured frames, two repeats.");
            File.WriteAllBytes(Path.Combine(output,"build-info.json"),File.ReadAllBytes(Path.Combine(root,"build-info.json")));
            File.WriteAllBytes(Path.Combine(output,"suite.tsv"),File.ReadAllBytes(Path.Combine(root,"suite.tsv")));
            Note("player_sha256="+Hash(Path.Combine(root,"main.exe")));
            string[] jobs=File.ReadAllLines(Path.Combine(root,"suite.tsv"));
            if(jobs.Length!=5)throw new Exception("Expected five explicit ROM cases");
            Export();
            for(int game=0;game<jobs.Length;++game) {
                string[] job=jobs[game].Split('\t');
                if(job.Length!=10)throw new Exception("Invalid suite row");
                string rom=Path.Combine(root,job[1]),input=Path.Combine(root,job[2]);
                for(int repeat=1;repeat<=2;++repeat) {
                    string stem="case-"+game.ToString("D2")+"-r"+repeat;
                    current=Path.Combine(output,stem+".log");
                    AppDomain domain=null;
                    try {
                        Record("START {0} repeat={1} warmup={2} frames=300",job[0],repeat,job[3]);
                        Record("rom_sha256={0} input_sha256={1}",Hash(rom),Hash(input));
                        if(Hash(rom)!=job[4] || Hash(input)!=job[5])throw new Exception("Input provenance mismatch");
                        Export();
                        // Main.exe contains static chip state. A fresh domain reproduces a cold game launch.
                        string bench=null;
                        if (desktop) {
                            domain=AppDomain.CreateDomain(stem);
                            var runner=(CaseRunner)domain.CreateInstanceFromAndUnwrap(Assembly.GetExecutingAssembly().Location,typeof(CaseRunner).FullName);
                            bench=runner.Execute(root,output,stem,job,true);
                        } else {
                            IntPtr config=Marshal.StringToHGlobalAnsi(root+"\t"+output+"\t"+stem+"\t"+jobs[game]);
                            int okay;
                            try { okay=NativeRunCase(config); } finally { Marshal.FreeHGlobal(config); }
                            if(okay==0)throw new Exception("Native isolated case failed; see case log");
                            foreach(string line in File.ReadAllLines(current)) if(line.StartsWith("BENCH system="))bench=line;
                        }
                        if(bench==null)throw new Exception("Missing benchmark result");
                        string expected="video_hash="+job[6]+" audio_hash="+job[7]+" samples="+job[8]+" nonzero="+job[9];
                        bool match=bench.Contains(expected);
                        if(!match)++differences;
                        Record("CASE COMPLETE {0} reference={1}",stem,match?"MATCH":"DIFFER");
                    } catch(Exception e) { ++errors;Record("CASE ERROR {0}: {1}",stem,e); }
                    finally { if(domain!=null)AppDomain.Unload(domain); }
                    current=null;
                    Export();
                }
            }
            Note("SUITE COMPLETE cases=10 errors="+errors+" reference_differences="+differences);
            Note("Reference is PS4 Mono in shadPS4; DIFFER requires investigation, not a speedup claim.");
            File.WriteAllText(Path.Combine(output,"complete.txt"),"cases=10 errors="+errors+" reference_differences="+differences+"\n");
            Note(errors==0?"RESULT PASS":"RESULT FAIL");
            Export();
            summary=null;
        }
    }
    public static class Desktop { public static void Main(string[] args) { Entry.Run(args[0],args[1],true); } }
}
