#pragma once
// The few switches the borrowed overlay code reads. Dye All has no settings file yet;
// these are fixed defaults (the same ones Master Looter ships with).
namespace ml
{
    struct Config
    {
        bool wrapSwapChain = true;   // draw through Master Looter's swapchain wrapper
        bool enableDred    = false;  // GPU crash breadcrumbs, off
        bool hookXInput    = true;   // keep pad input away from the game while the panel has it
    };

    namespace Settings
    {
        inline Config& Get()
        {
            static Config c;
            return c;
        }
    }
}
