// UC Vib-Ribbon, Nintendo DSi port.
//
//   top screen    : the game (PS1 picture scaled to 256x192; thin lines are kept, not skipped)
//   bottom screen : only the animated Vibri logo, so it is not black while you play
//   SD card       : sd:/VIBRI/vibribbon.cue + vibribbon.bin   (+ your PS1 BIOS .bin in the same folder)
//
// If the cue/bin are not at exactly that path the top screen says so and nothing else happens.
// Everything else from the PC launcher (mods, MP3 own-CD, neon glow, screenshots, fast-forward) is left out.
//
// Sound is NOT output yet: the game still runs its audio side every frame (it waits on it) but nothing is played.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "cpu.h"        // core first, so libnds macros cannot leak into it
#include "pad_map.h"
#include "logo_anim.h"

#include <nds.h>
#include <fat.h>
#include <dirent.h>

#ifndef BUS_CLOCK
#define BUS_CLOCK 33513982
#endif

namespace {

const char* const kNoGame = "THERES NO GAME SILLY BITCH!!";
const char* const kDirs[] = {"sd:/VIBRI/", "fat:/VIBRI/"};   // DSi SD card; the second is for DLDI/flashcart runs

// ---------------------------------------------------------------------------------------------------------------
// Bottom screen: the logo (src/logo_anim.h: 18 grey frames, 223x191, run-length coded as (value, run) pairs).
// ---------------------------------------------------------------------------------------------------------------
uint16_t* g_sub_fb = nullptr;
uint8_t g_logo_img[256 * 192] __attribute__((aligned(4)));
int g_logo_frame = 0;
uint32_t g_logo_last = 0;

void logo_draw(int n) {
    std::memset(g_logo_img, 0, sizeof g_logo_img);
    const uint8_t* d = logo_anim::kData[n];
    const unsigned sz = logo_anim::kSize[n];
    const int x0 = (256 - logo_anim::kW) / 2;
    int x = 0, y = 0;
    for (unsigned i = 0; i + 1 < sz && y < logo_anim::kH; i += 2) {
        const uint8_t v = d[i];
        for (int r = d[i + 1]; r > 0; --r) {
            if (v && y < 192) g_logo_img[y * 256 + x0 + x] = v;
            if (++x >= logo_anim::kW) { x = 0; ++y; if (y >= logo_anim::kH) break; }
        }
    }
    const uint16_t* s = (const uint16_t*)g_logo_img;   // VRAM only takes 16-bit writes
    for (int i = 0; i < 256 * 192 / 2; ++i) g_sub_fb[i] = s[i];
}

void logo_init() {
    videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    const int bg = bgInitSub(3, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
    g_sub_fb = (uint16_t*)bgGetGfxPtr(bg);
    for (int v = 0; v < 256; ++v) { const int g = (v * 235 / 255) >> 3; BG_PALETTE_SUB[v] = RGB15(g, g, g); }
    cpuStartTiming(0);
    g_logo_last = cpuGetTiming();
    logo_draw(0);
}

// Call as often as convenient; advances the logo every 100 ms.
void logo_tick() {
    const uint32_t step = BUS_CLOCK / 10;
    const uint32_t now = cpuGetTiming();
    uint32_t elapsed = now - g_logo_last;
    if (elapsed < step) return;
    const uint32_t n = elapsed / step;
    g_logo_last += n * step;
    g_logo_frame = (int)((g_logo_frame + n) % logo_anim::kFrames);
    logo_draw(g_logo_frame);
}

// ---------------------------------------------------------------------------------------------------------------
// Top screen
// ---------------------------------------------------------------------------------------------------------------
PrintConsole g_console;
uint16_t* g_fb = nullptr;

void top_console() {
    videoSetMode(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    consoleInit(&g_console, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
    consoleClear();
}

void top_text(int row, const char* text) {
    int col = (32 - (int)std::strlen(text)) / 2;
    if (col < 0) col = 0;
    iprintf("\x1b[%d;%dH%s", row, col, text);
}

[[noreturn]] void message(const char* l1, const char* l2 = nullptr, const char* l3 = nullptr) {
    top_console();
    top_text(10, l1);
    if (l2) top_text(12, l2);
    if (l3) top_text(13, l3);
    for (;;) { logo_tick(); swiWaitForVBlank(); }
}

void top_game() {
    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    const int bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    g_fb = (uint16_t*)bgGetGfxPtr(bg);
    for (int i = 0; i < 256 * 256; ++i) g_fb[i] = 0x8000;   // black (bit 15 = visible)
}

// PS1 picture -> 256x192. Each output pixel looks at the (up to) 2x2 source pixels it covers and keeps the brightest,
// because the game is thin white lines and a plain nearest-neighbour pick would drop many of them.
uint16_t g_c0[256], g_c1[256];
uint16_t g_r0[192], g_r1[192];
int g_last_x = -1, g_last_y = -1, g_last_w = -1, g_last_h = -1;

void present(const hw::Gpu& gpu) {
    const hw::Gpu::DispInfo d = gpu.display_info();
    if (d.off || d.depth24) { for (int i = 0; i < 256 * 192; ++i) g_fb[i] = 0x8000; return; }
    if (d.x != g_last_x || d.y != g_last_y || d.w != g_last_w || d.h != g_last_h) {
        g_last_x = d.x; g_last_y = d.y; g_last_w = d.w; g_last_h = d.h;
        for (int x = 0; x < 256; ++x) {
            const int s0 = x * d.w / 256, e = ((x + 1) * d.w + 255) / 256;
            const int s1 = std::min(std::max(s0, e - 1), d.w - 1);
            g_c0[x] = (uint16_t)((d.x + s0) & 1023); g_c1[x] = (uint16_t)((d.x + s1) & 1023);
        }
        for (int y = 0; y < 192; ++y) {
            const int s0 = y * d.h / 192, e = ((y + 1) * d.h + 191) / 192;
            const int s1 = std::min(std::max(s0, e - 1), d.h - 1);
            g_r0[y] = (uint16_t)((d.y + s0) & 511); g_r1[y] = (uint16_t)((d.y + s1) & 511);
        }
    }
    const uint16_t* vr = gpu.vram.data();
    for (int y = 0; y < 192; ++y) {
        const uint16_t* ra = vr + (size_t)g_r0[y] * 1024;
        const uint16_t* rb = vr + (size_t)g_r1[y] * 1024;
        uint16_t* o = g_fb + y * 256;
        for (int x = 0; x < 256; ++x) {
            const int c0 = g_c0[x], c1 = g_c1[x];
            unsigned m = ra[c0] & 0x7FFF;
            unsigned t = ra[c1] & 0x7FFF; if (t > m) m = t;
            t = rb[c0] & 0x7FFF; if (t > m) m = t;
            t = rb[c1] & 0x7FFF; if (t > m) m = t;
            o[x] = (uint16_t)(m | 0x8000);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Input: DS buttons -> PS1 pad (same layout as the PC build: A=Cross B=Circle X=Square Y=Triangle, L/R = L1/R1)
// ---------------------------------------------------------------------------------------------------------------
uint16_t read_pad() {
    scanKeys();
    const uint32_t k = keysHeld();
    pad::XboxState s{};
    s.a = k & KEY_A; s.b = k & KEY_B; s.x = k & KEY_X; s.y = k & KEY_Y;
    s.lb = k & KEY_L; s.rb = k & KEY_R;
    s.back = k & KEY_SELECT; s.start = k & KEY_START;
    s.dpad_up = k & KEY_UP; s.dpad_down = k & KEY_DOWN; s.dpad_left = k & KEY_LEFT; s.dpad_right = k & KEY_RIGHT;
    return pad::to_ps1(s);
}

// ---------------------------------------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------------------------------------
bool file_exists(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// A PS1 BIOS is exactly 512 KB and contains the Sony copyright string.
bool looks_like_bios(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    bool ok = false;
    if (len == 512 * 1024) {
        std::fseek(f, 0, SEEK_SET);
        std::string data((size_t)len, '\0');
        ok = std::fread(&data[0], 1, (size_t)len, f) == (size_t)len && data.find("Sony Computer Entertainment") != std::string::npos;
    }
    std::fclose(f);
    return ok;
}

std::string find_bios(const std::string& dir) {
    if (looks_like_bios(dir + "bios.bin")) return dir + "bios.bin";
    DIR* dp = opendir(dir.c_str());
    if (!dp) return "";
    std::string hit;
    while (struct dirent* e = readdir(dp)) {
        std::string n = e->d_name, low = n;
        for (auto& c : low) c = (char)std::tolower((unsigned char)c);
        if (low.size() < 5 || low.substr(low.size() - 4) != ".bin" || low == "vibribbon.bin") continue;
        if (looks_like_bios(dir + n)) { hit = dir + n; break; }
    }
    closedir(dp);
    return hit;
}

// Optional sd:/VIBRI/dsi.ini with a line "cpi=N": emulated CPU cycles per instruction (default 2 = what the PC build uses).
// A bigger number makes each game frame need fewer emulated instructions, which is faster but makes the game's own CPU
// work "slower". Only worth touching if the game runs too slowly.
int read_cpi(const std::string& dir) {
    std::FILE* f = std::fopen((dir + "dsi.ini").c_str(), "rb");
    if (!f) return 0;
    char buf[256] = {};
    const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    std::fclose(f);
    const char* p = std::strstr(buf, "cpi=");
    if (!p) return 0;
    const int v = std::atoi(p + 4);
    return (v >= 1 && v <= 64) ? v : 0;
}

}  // namespace

int main() {
    if (isDSiMode()) setCpuClock(true);   // ARM9 at 134 MHz instead of 67 MHz
    logo_init();
    top_console();

    if (!isDSiMode()) message("NEEDS DSI MODE", "(16 MB RAM)", "run it in DSi mode");
    {   // the game needs about 5 MB of working memory
        void* probe = std::malloc(6 * 1024 * 1024);
        if (!probe) message("NOT ENOUGH RAM", "(needs the DSi's 16 MB)");
        std::free(probe);
    }
    if (!fatInitDefault()) message(kNoGame);

    std::string dir;
    for (const char* d : kDirs) if (file_exists(std::string(d) + "vibribbon.cue")) { dir = d; break; }
    if (dir.empty()) message(kNoGame);

    top_text(11, "LOADING...");

    auto img = std::make_unique<disc::Image>();
    if (!img->open(dir + "vibribbon.cue").empty()) message(kNoGame);   // cue/bin missing or unreadable at that exact path

    const std::string boot = disc::boot_file(*img);
    std::vector<uint8_t> exe_file;
    disc::PsxExe exe;
    if (boot.empty() || !img->read_file(boot, exe_file) || !disc::parse_psx_exe(exe_file, exe)) message("NO BOOTABLE PS1 EXE", "ON THE DISC");
    if (boot != "SCES_028.73" && boot != "SCPS_180.12") message("NOT A VIB-RIBBON DISC", boot.c_str());
    exe_file.clear(); exe_file.shrink_to_fit();

    auto cpu = std::make_unique<cpu::Cpu>();
    cpu->trace.enabled = false;
    cpu->attach_disc(img.get());
    if (const int cpi = read_cpi(dir)) cpu->cycles_per_instr = (uint32_t)cpi;
    cpu->idle_skip = true;   // fast-forward the game's wait-for-next-frame loop (exact, see cpu.h)

    const std::string bios = find_bios(dir);
    if (!bios.empty() && cpu->load_bios(bios)) {
        // Real BIOS: boot to the shell, then start the disc's executable under the real kernel (as on PC).
        // The kernel is what makes the game's controller driver run.
        unsigned spin = 0;
        while (!cpu->halted && cpu->pc != 0x80030000u && cpu->icount < 100000000ull) {
            cpu->step();
            if ((++spin & 0x3FFF) == 0) logo_tick();
        }
        if (cpu->halted || cpu->pc != 0x80030000u) message("BIOS DID NOT START", cpu->halted ? cpu->halt_reason.substr(0, 30).c_str() : "timeout");
        if (!cpu->sideload_exe(exe.text, exe.load_addr, exe.pc, exe.gp, exe.sp, 10, 4, 0x801FFFF0u)) message("COULD NOT START GAME", cpu->halt_reason.substr(0, 30).c_str());
    } else {
        cpu->load_exe(exe.text, exe.load_addr, exe.pc, exe.gp, exe.sp);
        top_text(13, "NO BIOS IN /VIBRI");
        top_text(14, "BUTTONS WILL NOT WORK");
        for (int i = 0; i < 150; ++i) { logo_tick(); swiWaitForVBlank(); }
    }

    top_game();

    pad::TapFilter tap;   // the game's menus step twice for a held d-pad press; see pad_map.h
    std::vector<int16_t> audio((size_t)882 * 2);
    unsigned frame_no = 0;
    for (;;) {
        cpu->pad1 = tap.apply(read_pad());
        if (!cpu->run_frame() && cpu->halted) {
            message("CPU STOPPED", cpu->halt_reason.substr(0, 30).c_str());
        }
        // The SPU envelopes and the CD audio position only advance in audio_frame(), and the game waits on both,
        // so it must run every frame even though the samples are thrown away.
        cpu->audio_frame(cpu->frame_samples(), audio.data());
        // Drawing the 256x192 picture costs about as much as emulating a frame, so only every 2nd frame is shown.
        if ((++frame_no & 1) == 0) present(cpu->gpu);
        logo_tick();
    }
}
