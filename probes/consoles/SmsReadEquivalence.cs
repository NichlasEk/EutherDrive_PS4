// Compare the candidate against the retained original mapper across its entire
// address space, including live writes and unsupported fast-path conditions.
using System;
using System.Reflection;

class SmsReadEquivalence {
    const BindingFlags Flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
    static Type main, cpuType;
    static object cpu;
    static Func<uint,byte> read;
    static Func<ushort,byte> reference;
    static Action<uint,byte> write;
    static long checks;
    static void SetMain(string name, object value) { main.GetField(name,Flags).SetValue(null,value); }
    static void SetCpu(string name, object value) { cpuType.GetField(name,Flags).SetValue(cpu,value); }
    static void Sweep(string label) {
        for (uint a=0;a<65536;a++) {
            byte expected=reference((ushort)a), actual=read(a);
            if (expected!=actual) throw new Exception(label+" @"+a.ToString("x4")+": "+expected+" != "+actual);
            checks++;
        }
    }
    static void Main(string[] args) {
        var assembly=Assembly.LoadFrom(args[0]);
        main=assembly.GetType("EutherDrive.Core.MdTracerCore.md_main",true);
        cpuType=assembly.GetType("EutherDrive.Core.MdTracerCore.md_z80",true);
        cpu=Activator.CreateInstance(cpuType,true);
        SetMain("g_md_z80",cpu); SetMain("g_masterSystemMode",true);
        SetCpu("g_ram",new byte[8192]);
        read=(Func<uint,byte>)Delegate.CreateDelegate(typeof(Func<uint,byte>),cpu,cpuType.GetMethod("read8",Flags));
        reference=(Func<ushort,byte>)Delegate.CreateDelegate(typeof(Func<ushort,byte>),cpu,cpuType.GetMethod("ReadSmsMemoryOriginal",Flags));
        write=(Action<uint,byte>)Delegate.CreateDelegate(typeof(Action<uint,byte>),cpu,cpuType.GetMethod("write8",Flags));
        var mapper=main.GetField("g_masterSystemMapper",Flags);
        foreach(int size in new[]{0,8192,16384,32768,49152,65536,131072,262144}) {
            var rom=new byte[size]; var random=new Random(1234+size); random.NextBytes(rom);
            SetMain("g_masterSystemRom",rom); SetMain("g_masterSystemRomSize",size);
            foreach (int bank in new[]{0,1,2,7,255}) {
                write(0xfffc,0); write(0xfffd,(byte)bank); write(0xfffe,(byte)(bank+1)); write(0xffff,(byte)(bank+2));
                Sweep("size="+size+" bank="+bank);
            }
            for(uint a=0xc000;a<0xe000;a++)write(a,(byte)(a^(a>>8)));
            Sweep("RAM aliases "+size);
            foreach(byte control in new byte[]{8,12,0}) {
                write(0xfffc,control);
                for(uint a=0x8000;a<0xc000;a++)write(a,(byte)(a>>5));
                Sweep("cart RAM control="+control+" size="+size);
            }
            write(0x1003e,0x40); Sweep("disabled cartridge "+size);
            write(0x1003e,0x08); Sweep("reenabled cartridge "+size);
            foreach(var value in Enum.GetValues(mapper.FieldType)) {
                mapper.SetValue(null,value); Sweep("mapper="+value+" size="+size);
            }
            mapper.SetValue(null,Enum.ToObject(mapper.FieldType,0));
        }
        Console.WriteLine("PASS "+checks+" reads equal original mapper (banks, sizes, RAM, enable, fallback mappers)");
    }
}
