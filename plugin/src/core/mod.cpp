#include "mod.h"

#include <MinHook.h>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "paths.h"
#include "state.h"
#include "../dye/dyedb.h"
#include "../dye/hooks.h"
#include "../game/game.h"
#include "../hooks/dx12_hook.h"
#include "../version.h"

namespace ml::Mod
{
    static bool g_initialized = false;

    static bool HostIsGame()
    {
        char exe[MAX_PATH] = "";
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        const char* name = strrchr(exe, '\\');
        name = name ? name + 1 : exe;
        return _stricmp(name, "CrimsonDesert.exe") == 0;
    }

    static DWORD WINAPI Worker(LPVOID)
    {
        // Touch game code only once the game is drawing, as Master Looter does.
        for (int i = 0; i < 300 && !State::Get().overlayReady; ++i) Sleep(200);
        if (!State::Get().overlayReady) LOG_ERR("No frame in sixty seconds; starting anyway. The panel may not draw.");
        const bool ok = dye::game::ResolveAll();
        LOG(ok ? "[sig] event system resolved" : "[sig] event system NOT fully resolved; the event log stays empty");
        dye::hooks::Install();
        LOG_OK("Dye All ready. Open the dye screen at a camp dyer; F7 shows the panel anywhere.");
        return 0;
    }

    void Initialize(HMODULE module)
    {
        if (g_initialized || !HostIsGame()) return;
        Paths::Init(module);
        Log::Claim();
        LOG("Dye All %s for Crimson Desert %s starting.", DYEALL_VERSION, DYEALL_GAME_BUILD);
        dye::db::Load();

        if (MH_Initialize() != MH_OK) { LOG_ERR("MinHook initialization failed."); return; }
        if (!hooks::InstallDX12Hooks())
        {
            LOG_ERR("DX12 hooks failed; no panel this session.");
            MH_Uninitialize();
            return;
        }
        State::Get().hooksOk = true;
        g_initialized = true;
        if (HANDLE t = CreateThread(nullptr, 0, &Worker, nullptr, 0, nullptr)) CloseHandle(t);
    }

    void OnRenderProcess()
    {
        static bool s_done = false;
        if (s_done) return;
        s_done = true;
        State::Get().overlayReady = true;
        LOG("[render] first frame reached the overlay");
    }

    void Shutdown(bool terminating)
    {
        if (!g_initialized) return;
        g_initialized = false;
        if (terminating) { Log::Shutdown(); return; }
        dye::hooks::Remove();
        hooks::RemoveDX12Hooks();
        MH_Uninitialize();
        Log::Shutdown();
    }
}
