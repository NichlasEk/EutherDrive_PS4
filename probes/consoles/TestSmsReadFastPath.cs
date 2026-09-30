// Run against the built player assembly under desktop Mono, with a private SMS ROM.
using System;
using System.Diagnostics;
using System.Reflection;
using EutherDrive.Core;
class TestSmsReadFastPath {
    const BindingFlags Fields=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance;
    static void Set(object target,Type type,string name,object value) { type.GetField(name,Fields).SetValue(target,value); }
    static long Bench(Func<ushort,byte> read,out uint checksum) {
        checksum=0;var watch=Stopwatch.StartNew();
        for(int i=0;i<4000000;++i)checksum=unchecked(checksum+read((ushort)(i%0xc000)));
        return watch.ElapsedTicks;
    }
    static void Main(string[] args) {
        var core=new MdTracerAdapter();core.LoadRom(args[0]);
        var main=typeof(MdTracerAdapter).Assembly.GetType("EutherDrive.Core.MdTracerCore.md_main");
        var z80=main.GetField("g_md_z80",Fields).GetValue(null);var type=z80.GetType();
        var fast=(Func<ushort,byte>)Delegate.CreateDelegate(typeof(Func<ushort,byte>),z80,type.GetMethod("ReadSmsMemory",Fields));
        var old=(Func<ushort,byte>)Delegate.CreateDelegate(typeof(Func<ushort,byte>),z80,type.GetMethod("ReadSmsMemoryOriginal",Fields));
        string[] tracked={"_lastReadAddr","_lastReadValue","_lastReadPc","_lastReadWasBanked","_lastReadM68kAddr"};
        int checkedReads=0;
        Action<ushort> check=address=>{
            byte expected=old(address);object[] state=new object[tracked.Length];
            for(int i=0;i<state.Length;++i)state[i]=type.GetField(tracked[i],Fields).GetValue(z80);
            if(fast(address)!=expected)throw new Exception("Read mismatch at "+address);
            for(int i=0;i<state.Length;++i)
                if(!Equals(state[i],type.GetField(tracked[i],Fields).GetValue(z80)))throw new Exception("Tracking mismatch: "+tracked[i]);
            ++checkedReads;
        };
        foreach(int size in new[]{512,1024,16384,32768,49152,65536,131072,262144,1048576,0,32768}) {
            var rom=new byte[size];for(int i=0;i<size;++i)rom[i]=(byte)((i*13)^(i>>8)^(i>>16));
            Set(null,main,"g_masterSystemRom",rom);Set(null,main,"g_masterSystemRomSize",size);
            for(int bank=0;bank<256;++bank) {
                Set(z80,type,"_smsBank0",(byte)bank);Set(z80,type,"_smsBank1",(byte)(255-bank));Set(z80,type,"_smsBank2",(byte)(bank^85));
                foreach(ushort a in new ushort[]{0,1,0x1ff,0x3ff,0x400,0x3fff,0x4000,0x7fff,0x8000,0xbfff,0xc000,0xffff})check(a);
            }
            for(int a=0;a<65536;++a)check((ushort)a);
            Set(z80,type,"_smsCartridgeEnabled",false);check(0);check(0x8000);Set(z80,type,"_smsCartridgeEnabled",true);
            Set(z80,type,"_smsSegaRamEnabled",true);check(0x400);check(0x8000);check(0xbfff);Set(z80,type,"_smsSegaRamEnabled",false);
        }
        var mapper=main.GetField("g_masterSystemMapper",Fields);var saved=mapper.GetValue(null);
        mapper.SetValue(null,Enum.Parse(mapper.FieldType,"Codemasters"));
        for(int a=0;a<65536;++a)check((ushort)a);mapper.SetValue(null,saved);
        Console.WriteLine("PASS actual core fast/original read and tracking equivalence: "+checkedReads);
        uint checksum;Bench(fast,out checksum);Bench(old,out checksum);
        foreach(var read in new[]{old,fast,fast,old}) {
            long ticks=Bench(read,out checksum);
            Console.WriteLine("BENCH "+(read==old?"original":"fast")+" ms="+(ticks*1000.0/Stopwatch.Frequency).ToString("F1")+" checksum="+checksum);
        }
    }
}
