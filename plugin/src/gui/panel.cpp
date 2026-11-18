// The Dye All side panel, drawn with Dear ImGui through the borrowed DX12 overlay.
// Shows every dyeable item the player owns while the dye screen is
// open; it takes the mouse while the cursor is over it. F7 toggles it anywhere.
#include "menu.h"

#include <Windows.h>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "../core/log.h"
#include "../core/state.h"
#include "../dye/actions.h"
#include "../dye/hooks.h"
#include "../dye/stock.h"
#include "../game/game.h"
#include "../hooks/input.h"
#include "../version.h"

namespace ml::gui
{
    static float g_scale = 1.0f;
    static ImFont* g_body = nullptr;
    static ImFont* g_head = nullptr;
    static bool g_manual = false;     // opened with F7
    static bool g_graceHidden = false; // the short panel after the dye screen closed was dismissed
    static bool g_wasDye = false;
    static bool g_hover = false;      // the real cursor is over the panel (dye screen only)
    static ImVec2 g_rectMin, g_rectMax;
    static int g_selBucket = -1, g_selSlot = -1;

    // ------------------------------------------------------------ sorting ----
    // Type groups all helmets, then all body armor, and so on, so a whole kind
    // can be dyed in one go. Order follows the dyer: armor, weapons, horse, A.T.A.G.
    enum SortMode { kSortType = 0, kSortName = 1, kSortPlace = 2 };
    static int g_sort = kSortType;
    struct TypeInfo { uint32_t hash; int order; const char* label; };
    static const TypeInfo kTypes[] = {
        { 1963713749u, 0, "Helmets" }, { 2667368044u, 1, "Body armor" }, { 3628286577u, 2, "Gloves" },
        { 3438125342u, 3, "Boots" }, { 1241706053u, 4, "Cloaks" }, { 503331443u, 5, "Glasses" },
        { 1584411264u, 10, "Swords" }, { 2594511993u, 11, "Axes" }, { 2327795645u, 12, "Maces" },
        { 1187101662u, 13, "Rapiers" }, { 1921528741u, 14, "Daggers" },
        { 257028056u, 20, "Shields" }, { 1334259611u, 21, "Tower shields" }, { 517658843u, 22, "Right-hand shields" },
        { 249090674u, 30, "Bows" }, { 389085181u, 31, "Crossbows" }, { 2276022818u, 32, "Muskets" },
        { 3956795411u, 33, "Shotguns" }, { 31650711u, 34, "Pistols" }, { 190908618u, 35, "Hand cannons" },
        { 1086980073u, 40, "Longswords" }, { 643712710u, 41, "Greatswords" }, { 585399773u, 42, "Two-handed axes" },
        { 2914941932u, 43, "Spears" }, { 3456689820u, 44, "Halberds" }, { 3150053877u, 45, "War hammers" },
        { 604374103u, 46, "Greathammers" }, { 975012496u, 47, "Cannons" },
        { 2841735235u, 50, "Horse champrons" }, { 3651061774u, 51, "Horse barding" }, { 4283032781u, 52, "Saddles" },
        { 189089223u, 53, "Stirrups" },
        { 1961773671u, 60, "A.T.A.G. plating" }, { 3659357466u, 61, "A.T.A.G. thrusterpacks" }, { 316730227u, 62, "A.T.A.G. boots" },
        { 786544299u, 63, "A.T.A.G. arms" }, { 1066416539u, 63, "A.T.A.G. arms" }, { 3370526075u, 63, "A.T.A.G. arms" },
        { 261596318u, 63, "A.T.A.G. arms" }, { 3392409000u, 63, "A.T.A.G. arms" }, { 3452336502u, 63, "A.T.A.G. arms" },
        { 3186226560u, 63, "A.T.A.G. arms" },
    };
    static const TypeInfo& TypeOf(uint32_t hash)
    {
        static const TypeInfo kOther{ 0, 99, "Other" };
        for (const TypeInfo& t : kTypes) if (t.hash == hash) return t;
        return kOther;
    }
    static const char* PlaceShort(int invType)
    {
        switch (invType)
        {
        case 1: return "Bag";
        case 7: return "Chest";
        case 8: return "Warehouse";
        case 14: return "Wardrobe";
        default: return "";
        }
    }
    static int PlaceOrder(int invType) { return invType == 14 ? 0 : invType == 7 ? 1 : invType == 1 ? 2 : 3; }
    static std::vector<const dye::stock::Entry*> Sorted(const std::vector<dye::stock::Entry>& list)
    {
        std::vector<const dye::stock::Entry*> v;
        v.reserve(list.size());
        for (const auto& e : list) v.push_back(&e);
        auto byName = [](const dye::stock::Entry* a, const dye::stock::Entry* b) { return _stricmp(a->item->Label(), b->item->Label()); };
        std::stable_sort(v.begin(), v.end(), [&](const dye::stock::Entry* a, const dye::stock::Entry* b)
        {
            const int ta = TypeOf(a->item->equipType).order, tb = TypeOf(b->item->equipType).order;
            const int pa = PlaceOrder(a->invType), pb = PlaceOrder(b->invType);
            const int n = byName(a, b);
            if (g_sort == kSortPlace) { if (pa != pb) return pa < pb; if (ta != tb) return ta < tb; return n < 0; }
            if (g_sort == kSortName)  { if (n) return n < 0; return pa < pb; }
            if (ta != tb) return ta < tb;
            if (n) return n < 0;
            return pa < pb;
        });
        return v;
    }

    static const ImVec4 kBg      (0.075f, 0.062f, 0.055f, 0.94f);
    static const ImVec4 kText    (0.90f, 0.86f, 0.78f, 1.0f);
    static const ImVec4 kTextDim (0.60f, 0.56f, 0.50f, 1.0f);
    static const ImVec4 kGold    (0.86f, 0.70f, 0.38f, 1.0f);
    static const ImVec4 kBronze  (0.45f, 0.34f, 0.22f, 1.0f);
    static const ImVec4 kFrame   (0.16f, 0.13f, 0.11f, 1.0f);
    static const ImVec4 kCrimson (0.55f, 0.12f, 0.10f, 1.0f);

    static ImFont* LoadFont(const char* file, float px)
    {
        char dir[MAX_PATH] = "C:\\Windows";
        GetWindowsDirectoryA(dir, MAX_PATH);
        const std::string path = std::string(dir) + "\\Fonts\\" + file;
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
        ImFontConfig cfg; cfg.OversampleH = 2; cfg.OversampleV = 1;
        return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), px, &cfg, ImGui::GetIO().Fonts->GetGlyphRangesDefault());
    }

    static void BuildFonts()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();
        g_body = LoadFont("segoeui.ttf", 17.0f * g_scale);
        if (!g_body) { ImFontConfig d; d.SizePixels = 17.0f * g_scale; g_body = io.Fonts->AddFontDefault(&d); }
        g_head = LoadFont("georgia.ttf", 22.0f * g_scale);
        if (!g_head) g_head = g_body;
    }

    bool FontsNeedRebuild() { return false; }
    void RebuildFonts() { BuildFonts(); }

    void InitStyle(float scale)
    {
        g_scale = scale < 0.8f ? 0.8f : (scale > 3.0f ? 3.0f : scale);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        BuildFonts();
        ImGui::StyleColorsDark();
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding = 2.0f; s.FrameRounding = 2.0f; s.WindowBorderSize = 1.0f;
        s.WindowPadding = ImVec2(16, 12); s.ItemSpacing = ImVec2(10, 6); s.ScrollbarSize = 12.0f;
        ImVec4* c = s.Colors;
        c[ImGuiCol_Text] = kText;             c[ImGuiCol_TextDisabled] = kTextDim;
        c[ImGuiCol_WindowBg] = kBg;           c[ImGuiCol_Border] = kBronze;
        c[ImGuiCol_FrameBg] = kFrame;         c[ImGuiCol_Button] = kFrame;
        c[ImGuiCol_ButtonHovered] = ImVec4(kCrimson.x, kCrimson.y, kCrimson.z, 0.6f);
        c[ImGuiCol_ButtonActive] = kCrimson;
        c[ImGuiCol_Header] = ImVec4(kCrimson.x, kCrimson.y, kCrimson.z, 0.40f);
        c[ImGuiCol_HeaderHovered] = ImVec4(kCrimson.x, kCrimson.y, kCrimson.z, 0.60f);
        c[ImGuiCol_HeaderActive] = kCrimson;
        c[ImGuiCol_ScrollbarGrab] = kBronze;  c[ImGuiCol_Separator] = kBronze;
        c[ImGuiCol_TableHeaderBg] = kFrame;   c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
        s.ScaleAllSizes(g_scale);
    }

    // After the dye screen closes with an item swapped in, a one-line panel
    // offers Put back now / Hide until the grace period runs out.
    static bool InGrace()
    {
        return !dye::hooks::DyeScreenOpen() && dye::actions::Current().active && dye::hooks::SinceDyeClosed() < dye::hooks::kGraceMs;
    }

    void PollToggle()
    {
        State& st = State::Get();
        const bool dyeOpen = dye::hooks::DyeScreenOpen();
        if (dyeOpen) g_graceHidden = false;
        const bool grace = InGrace() && !g_graceHidden;

        static bool s_f7 = false, s_esc = false;
        const bool front = State::ForegroundIsOurs();
        const bool f7 = front && (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        const bool esc = front && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        if (f7 && !s_f7 && State::HotkeysFree(VK_F7))
        {
            if (grace && !g_manual) { g_graceHidden = true; LOG("[panel] F7: short panel hidden"); }
            else
            {
                g_manual = !g_manual;
                LOG("[panel] F7: panel %s", g_manual ? "open, with the mouse" : "closed");
                if (g_manual) dye::stock::Refresh(true);
            }
        }
        if (esc && !s_esc)
        {
            if (g_manual) g_manual = false;
            else if (grace) g_graceHidden = true;
        }
        s_f7 = f7; s_esc = esc;

        const bool dye = dyeOpen || (InGrace() && !g_graceHidden);
        if (dyeOpen && !g_wasDye) dye::stock::Refresh(true);
        g_wasDye = dyeOpen;

        // Over the dye screen the panel takes the mouse only while the cursor is
        // on it (or a button pressed on it is still held); the game keeps it
        // everywhere else. F7 hands it the mouse outright, anywhere.
        bool hover = false;
        if (dye && !g_manual)
        {
            float x = 0, y = 0;
            const bool held = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (input::OsCursorOverlay(&x, &y))
                hover = x >= g_rectMin.x && x < g_rectMax.x && y >= g_rectMin.y && y < g_rectMax.y;
            if (!hover && g_hover && held) hover = true;
        }
        g_hover = hover;
        input::SetOsCursorMode(hover);
        st.menuOpen = g_manual || dye;
        st.menuWatch = !(g_manual || hover);
    }

    static void RenderShort()
    {
        const ImGuiIO& io = ImGui::GetIO();
        const dye::actions::Worn worn = dye::actions::Current();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 24.0f * g_scale, 90.0f * g_scale), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
        if (State::Get().menuWatch) flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("Dye All short", nullptr, flags))
        {
            const unsigned long left = (dye::hooks::kGraceMs - dye::hooks::SinceDyeClosed()) / 1000 + 1;
            ImGui::TextColored(kGold, "%s stays on for %lu s.", worn.name.c_str(), left);
            ImGui::TextColored(kTextDim, "Choose Dye again to keep dyeing it, or:");
            ImGui::BeginDisabled(dye::actions::Busy());
            if (ImGui::Button("Put back now")) dye::actions::RestoreNow();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Hide")) g_graceHidden = true;
            ImGui::SameLine();
            ImGui::TextColored(kTextDim, "(F7 / Esc)");
            g_rectMin = ImGui::GetWindowPos();
            g_rectMax = ImVec2(g_rectMin.x + ImGui::GetWindowSize().x, g_rectMin.y + ImGui::GetWindowSize().y);
        }
        ImGui::End();
    }

    bool WantsDraw() { return State::Get().menuOpen; }

    void FeedInput()
    {
        State& st = State::Get();
        ImGuiIO& io = ImGui::GetIO();
        const bool capt = st.Captures();
        io.MouseDrawCursor = capt && !g_hover;     // over the dye screen the Windows cursor stays visible
        static bool s_wasCapt = false;
        if (capt != s_wasCapt) { s_wasCapt = capt; if (capt && !g_hover) input::MenuOpened(); else if (!capt) input::MenuClosed(); }
        if (capt) input::FeedMouse(io);
        else { io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); io.AddFocusEvent(false); }
    }

    void Render()
    {
        State& st = State::Get();
        if (!st.menuOpen) return;

        static DWORD s_lastRefresh = 0;
        const DWORD now = GetTickCount();
        if (now - s_lastRefresh > 1000) { s_lastRefresh = now; dye::stock::Refresh(false); }

        if (!g_manual && InGrace()) { RenderShort(); return; }

        const ImGuiIO& io = ImGui::GetIO();
        const float w = 460.0f * g_scale;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - w - 24.0f * g_scale, 90.0f * g_scale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(w, io.DisplaySize.y * 0.72f), ImGuiCond_Always);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
        if (st.menuWatch) flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

        if (ImGui::Begin("Dye All", nullptr, flags))
        {
            ImGui::PushFont(g_head);
            ImGui::TextColored(kGold, "ALL DYEABLE ITEMS");
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextColored(kTextDim, "  Dye All %s", DYEALL_VERSION);
            ImGui::Separator();
            ImGui::TextWrapped("%s", dye::stock::Status());
            if (!dye::hooks::DyeHooksOk()) ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1), "The dye screen could not be hooked; see DyeAll.log.");
            if (g_manual) ImGui::TextColored(kTextDim, "F7 or Esc: give the mouse back to the game.");
            else if (!dye::hooks::DyeScreenOpen()) ImGui::TextColored(kTextDim, "F7: use the mouse on this list.");
            else ImGui::TextColored(kTextDim, "Point at this list to use it; the rest of the screen stays with the game.");
            ImGui::Spacing();

            const std::vector<dye::stock::Entry> list = dye::stock::Snapshot();
            const dye::actions::Worn worn = dye::actions::Current();
            const bool busy = dye::actions::Busy();
            ImGui::TextColored(kTextDim, "Sort by");
            ImGui::SameLine(); ImGui::RadioButton("Type", &g_sort, kSortType);
            ImGui::SameLine(); ImGui::RadioButton("Name", &g_sort, kSortName);
            ImGui::SameLine(); ImGui::RadioButton("Location", &g_sort, kSortPlace);
            const std::vector<const dye::stock::Entry*> rows = Sorted(list);
            const float footer = ImGui::GetFrameHeightWithSpacing() * 2.4f + ImGui::GetTextLineHeightWithSpacing() * 4.0f;
            if (ImGui::BeginChild("list", ImVec2(0, -footer), false))
            {
                const float placeX = ImGui::GetContentRegionAvail().x * 0.74f;
                int lastGroup = -1;
                for (size_t i = 0; i < rows.size(); ++i)
                {
                    const dye::stock::Entry& e = *rows[i];
                    const TypeInfo& ti = TypeOf(e.item->equipType);
                    const int group = g_sort == kSortType ? ti.order : g_sort == kSortPlace ? e.invType : -2;
                    if (group != lastGroup && g_sort != kSortName)
                    {
                        lastGroup = group;
                        size_t n = 0;
                        for (size_t k = i; k < rows.size(); ++k)
                        {
                            const int gk = g_sort == kSortType ? TypeOf(rows[k]->item->equipType).order : rows[k]->invType;
                            if (gk != group) break;
                            ++n;
                        }
                        ImGui::Spacing();
                        ImGui::TextColored(kGold, "%s (%zu)", g_sort == kSortType ? ti.label : dye::game::InvTypeName(e.invType), n);
                        ImGui::Separator();
                    }
                    ImGui::PushID(static_cast<int>(e.bucket * 10000 + e.slot));
                    const bool sel = e.bucket == g_selBucket && e.slot == g_selSlot;
                    const bool noAtag = dye::actions::NoAtag(e.item->equipType);
                    const bool swappable = dye::actions::GearSlotFor(e.item->equipType) >= 0 && !noAtag;
                    if (!swappable) ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
                    if (ImGui::Selectable(e.item->Label(), sel, ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        g_selBucket = e.bucket; g_selSlot = e.slot;
                        LOG("[panel] picked %s (%s slot %u)", e.item->Label(), dye::game::InvTypeName(e.invType), e.slot);
                        if (ImGui::IsMouseDoubleClicked(0) && !busy) dye::actions::TryOn(e);
                    }
                    if (!swappable) ImGui::PopStyleColor();
                    // Where it is (type and name views) or what it is (location view), dimmed on the right.
                    const bool hovered = ImGui::IsItemHovered();
                    ImGui::SameLine(placeX);
                    ImGui::TextColored(kTextDim, "%s", g_sort == kSortPlace ? ti.label : PlaceShort(e.invType));
                    if (hovered && !st.menuWatch)
                        ImGui::SetTooltip(swappable ? "Double-click to wear it here for dyeing."
                                          : noAtag ? "The dyer has no A.T.A.G. tab: you don't have an A.T.A.G. yet."
                                                   : "Crossbows have no slot on Kliff, Damiane or Oongka.");
                    ImGui::PopID();
                }
                if (list.empty()) ImGui::TextColored(kTextDim, "Nothing found yet.");
            }
            ImGui::EndChild();
            ImGui::Separator();

            const dye::stock::Entry* pick = nullptr;
            for (const dye::stock::Entry& e : list) if (e.bucket == g_selBucket && e.slot == g_selSlot) pick = &e;
            ImGui::BeginDisabled(busy);
            if (pick && dye::actions::GearSlotFor(pick->item->equipType) >= 0 && !dye::actions::NoAtag(pick->item->equipType))
            {
                if (ImGui::Button("Wear it to dye")) dye::actions::TryOn(*pick);
                ImGui::SameLine();
                ImGui::TextColored(kTextDim, "%s", pick->item->Label());
            }
            else ImGui::TextColored(kTextDim, "Pick an item, then Wear it to dye (or double-click it).");
            if (worn.active)
            {
                if (ImGui::Button("Put back now")) dye::actions::RestoreNow();
                ImGui::SameLine();
                ImGui::TextWrapped(worn.waitingToGoBack ? "%s is in your bag, waiting to go back to the %s." : "Wearing %s from the %s.",
                                   worn.name.c_str(), dye::game::InvTypeName(worn.fromType));
            }
            ImGui::EndDisabled();
            if (dye::hooks::DyeScreenOpen())
            {
                if (ImGui::SmallButton("Refresh")) dye::hooks::RequestRefresh(5, 0);
                ImGui::SameLine();
                ImGui::TextColored(kTextDim, "(if the dyer's list looks out of date)");
            }
            const std::string act = dye::actions::Status();
            ImGui::PushTextWrapPos(0.0f);
            if (!act.empty()) ImGui::TextColored(kGold, "%s", act.c_str());
            ImGui::PopTextWrapPos();
            g_rectMin = ImGui::GetWindowPos();
            g_rectMax = ImVec2(g_rectMin.x + ImGui::GetWindowSize().x, g_rectMin.y + ImGui::GetWindowSize().y);
        }
        ImGui::End();
    }
}
