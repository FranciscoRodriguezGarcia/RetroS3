"""Exercise the production frame-loop conditions with a mocked CPU/LCD.

PowerShell Add-Type runs the C loop body after minimal C-to-C# syntax changes.
This isolates frame termination; it is not a full emulator/ROM test.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "lcd-regression"
OUT.mkdir(parents=True, exist_ok=True)
source = (ROOT / "components/gnuboy/gnuboy.c").read_text(encoding="utf-8")
body = source[source.index("void gnuboy_run(bool draw)"):source.index("void gnuboy_set_pad(")]
body = body.replace("void gnuboy_run", "static void gnuboy_run")
body = body.replace("unsigned", "uint")
body = body.replace("!(R_LCDC & 0x80)", "((R_LCDC & 0x80) == 0)")
body = body.replace("(R_LCDC & 0x80) &&", "((R_LCDC & 0x80) != 0) &&")
body = body.replace("draw && GB.video.callback", "draw && GB.video.callback != null")
body = body.replace("(GB.video.callback)(GB.video.buffer)", "GB.video.callback(GB.video.buffer)")
stub = r"""
using System;
using System.Threading;
public class FrameRegression {
    static int R_LCDC, R_LY, mode, calls, published, flushed, vblanks;
    class Video { public bool enabled; public Action<object> callback; public object buffer = null; }
    class Audio { public int pos; }
    static class GB { public static Video video = new Video(); public static Audio audio = new Audio(); }
    static void video(object p) { published++; }
    static void gnuboy_flush_audio() { flushed++; }
    static void gb_hw_vblank() { vblanks++; }
    static int gb_cpu_emulate(int cycles) {
        calls++;
        if (mode == 1) { R_LCDC=0; R_LY=0; }
        else if (mode == 2 && R_LY >= 147) { R_LCDC=0; R_LY=0; }
        else if (mode == 3 || mode == 4) { }
        else R_LY=(R_LY+1)%154;
        return cycles;
    }
"""
checks = r"""
    static void Reset() {
        R_LCDC=mode==5 ? 0 : 128; R_LY=mode==4 ? 145 : 0;
        calls=published=flushed=vblanks=0; GB.video.callback=video;
    }
    public static int RunAll() {
        for (mode=0; mode<6; mode++) {
            Reset(); gnuboy_run(true);
            if (flushed!=1 || calls>308) return 10+mode;
            if (mode==0 && (published!=1 || vblanks!=1 || R_LY!=0)) return 20;
            if (mode==1 && (published!=0 || vblanks!=0 || calls!=1)) return 21;
            if (mode==2 && (published!=1 || R_LCDC!=0)) return 22;
            if (mode==3 && (published!=0 || calls!=308)) return 23;
            if (mode==4 && (published!=1 || calls!=308)) return 24;
            if (mode==5 && (published!=0 || calls!=1)) return 25;
        }
        return 0;
    }
    public static bool ReproducesHang() {
        var thread = new Thread(() => { mode=1; Reset(); gnuboy_run(true); });
        thread.IsBackground=true; thread.Start(); return !thread.Join(250);
    }
}
"""

def run(name, code, assertion):
    cs = OUT / (name + ".cs")
    ps = OUT / (name + ".ps1")
    cs.write_text(stub + code + checks, encoding="utf-8")
    ps.write_text("$ErrorActionPreference='Stop'\nAdd-Type -Path '" + str(cs).replace("'", "''") + "'\n" + assertion, encoding="utf-8")
    subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(ps)], check=True, timeout=30)

run("fixed", body, "if ([FrameRegression]::RunAll() -ne 0) { throw 'Frame regression failed' }")
print("PASS: normal frame; LCD off during visible/vblank; stalled LY in both loops; LCD initially off", flush=True)
old = body.replace("((R_LCDC & 0x80) != 0) && R_LY <= 144 && steps < max_steps", "R_LY <= 144")
old = old.replace("((R_LCDC & 0x80) != 0) && R_LY > 0 && steps < max_steps", "R_LY > 0")
old = old.replace("const uint max_steps = 154 * 2;", "")
run("without_guards", old, "if (-not [FrameRegression]::ReproducesHang()) { throw 'Expected unguarded loop to hang' }")
print("PASS: removing the guards reproduces the non-returning LCD-off loop")
