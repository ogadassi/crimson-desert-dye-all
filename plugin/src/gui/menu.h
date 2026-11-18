#pragma once
// The interface the borrowed DX12 overlay calls each frame. Implemented by panel.cpp.
namespace ml::gui
{
    void InitStyle(float scale);
    bool FontsNeedRebuild();
    void RebuildFonts();
    void PollToggle();
    bool WantsDraw();
    void FeedInput();
    void Render();
}
