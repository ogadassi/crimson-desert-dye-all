#pragma once
#include <Windows.h>

namespace ml::Mod
{
    void Initialize(HMODULE module);
    // terminating: the process is exiting; only flush files then.
    void Shutdown(bool terminating);
    // Called from the render hook the first time this process presents a frame.
    void OnRenderProcess();
}
