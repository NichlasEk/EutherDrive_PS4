// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Nichlas Eklöf
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using EutherDrive.Core.GbEmu;

namespace Orbis {
    public static class Program {
        private static readonly bool Host = Environment.GetEnvironmentVariable("EUTHERDRIVE_RUNTIME_PROBE_HOST") == "1";
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void NativeReport(IntPtr text);
        [MethodImpl(MethodImplOptions.NoInlining)]
        private static void NativeLine(string line) {
            var p = Marshal.StringToHGlobalAnsi(line);
            try { NativeReport(p); } finally { Marshal.FreeHGlobal(p); }
        }
        private static void Report(string line) {
            Console.WriteLine(line);
            if (!Host) NativeLine(line);
        }

        // Original 32 KiB ROM. No Nintendo boot ROM, logo or commercial data.
        // CPU fills tile 0 with stripes, sets DMG palette E4 and scrolls one
        // pixel while RIGHT is held. Emulator.LoadRom supplies post-boot state.
        private static byte[] MakeRom() {
            var rom = new byte[32768];
            rom[0x100] = 0xc3; rom[0x101] = 0x50; rom[0x102] = 0x01;
            var title = System.Text.Encoding.ASCII.GetBytes("EUTHER GB TEST");
            Array.Copy(title, 0, rom, 0x134, title.Length);
            byte[] code = {
                0xf3,                         // di
                0x31,0xfe,0xff,                // ld sp,$fffe
                0xaf,0xe0,0x40,                // xor a; ldh [$40],a (LCD off)
                0xe0,0x26,                    // sound off
                0x21,0x00,0x80,0x06,0x08,     // hl=$8000; b=8 rows
                0x3e,0xaa,0x22,0x3e,0x55,0x22,// alternating color indices 1,2
                0x05,0x20,0xf7,               // dec b; jr nz,row
                0x21,0x00,0x98,0x01,0x00,0x04,// hl=$9800; bc=1024 map entries
                0xaf,0x22,0x0b,0x78,0xb1,0x20,0xf9, // clear map to tile 0
                0x3e,0xe4,0xe0,0x47,          // DMG palette
                0x3e,0x91,0xe0,0x40,          // LCD on, BG tile data $8000
                0x3e,0x20,0xe0,0x00,          // select direction inputs
                0xf0,0x00,0x2f,0xe6,0x01,     // read RIGHT as 0/1
                0xe0,0x43,                    // SCX=right
                0x18,0xf3                     // jr input loop
            };
            Array.Copy(code, 0, rom, 0x150, code.Length);
            byte checksum = 0;
            for (int i = 0x134; i <= 0x14c; ++i) checksum = unchecked((byte)(checksum - rom[i] - 1));
            rom[0x14d] = checksum;
            return rom;
        }

        private static uint Hash(uint[] pixels) {
            uint hash = 2166136261;
            foreach (uint pixel in pixels)
                for (int shift = 0; shift < 32; shift += 8)
                    hash = unchecked((hash ^ (byte)(pixel >> shift)) * 16777619);
            return hash;
        }

        private static uint[] Expected(bool right) {
            var pixels = new uint[160 * 144];
            for (int y = 0; y < 144; ++y)
                for (int x = 0; x < 160; ++x)
                    pixels[y * 160 + x] = ((x + (right ? 1 : 0)) & 1) == 0 ? 0xff0fac8bU : 0xff306230U;
            return pixels;
        }

        public static void Main() {
            try {
                Report("EutherDrive GB core probe 0.07 - DMG deterministic test");
                string directory = Host ? Path.Combine(Path.GetTempPath(), "eutherdrive-gb-probe") : "/data/eutherdrive-ps4";
                Directory.CreateDirectory(directory);
                string path = Path.Combine(directory, "euther-stripes.gb");
                File.WriteAllBytes(path, MakeRom());
                using (var emulator = new Emulator()) {
                    emulator.LoadRom(path);
                    for (int frame = 1; frame <= 180; ++frame) {
                        bool right = frame > 60 && frame <= 120;
                        emulator.SetInputState(false, false, false, right, false, false, false, false);
                        emulator.RunFrame();
                        emulator.ConsumeAudioBuffer();
                        if (frame % 60 != 0) continue;
                        var actual = emulator.Ppu.GetFrameBuffer();
                        var expected = Expected(right);
                        for (int i = 0; i < expected.Length; ++i)
                            if (actual[i] != expected[i])
                                throw new Exception("frame=" + frame + " pixel=" + i + " actual=" + actual[i].ToString("x8") + " expected=" + expected[i].ToString("x8") + " BGP=" + emulator.Mmu.ReadByte(0xff47).ToString("x2") + " TILE=" + emulator.Mmu.ReadByte(0x8000).ToString("x2") + "/" + emulator.Mmu.ReadByte(0x8001).ToString("x2") + " " + emulator.GetDebugState());
                        Report("PASS GB frame=" + frame + " right=" + right + " hash=" + Hash(actual).ToString("x8"));
                    }
                }
                Report("RESULT PASS");
            } catch (Exception error) { Report("FAIL GB " + error); }
        }
    }
}
