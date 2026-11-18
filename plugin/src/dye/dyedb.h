#pragma once
#include <cstdint>
#include <string>

// Every item the game marks dyeable (ItemInfo._isDyeable), by runtime item row.
// Built offline by tools/build_dyeables.py and compiled into the plugin; a
// DyeAll.dyeables.tsv next to the plugin overrides the built-in copy.
namespace dye::db
{
    struct Item
    {
        int         row = -1;
        uint32_t    key = 0;
        uint32_t    equipType = 0;
        std::string stringKey;
        std::string name;
        const char* Label() const { return name.empty() ? stringKey.c_str() : name.c_str(); }
    };

    bool Load();
    int  Count();
    const Item* ByRow(int row);   // nullptr when the row is not dyeable
}
