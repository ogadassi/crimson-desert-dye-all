#include "dyedb.h"

#include <cstdlib>
#include <unordered_map>
#include <vector>

#include "../core/log.h"
#include "../core/paths.h"

namespace dye::db
{
    static std::vector<Item> g_items;
    static std::unordered_map<int, size_t> g_byRow;

    bool Load()
    {
        std::string text;
        bool fromFile = false;
        if (!ml::Paths::ReadDataText(L"DyeAll.dyeables.tsv", L"DYEABLES", text, &fromFile))
        {
            LOG_ERR("[db] dyeable item table missing");
            return false;
        }
        size_t p = 0;
        while (p < text.size())
        {
            size_t e = text.find('\n', p);
            if (e == std::string::npos) e = text.size();
            std::string line = text.substr(p, e - p);
            p = e + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> f;
            size_t q = 0;
            while (true)
            {
                const size_t t = line.find('\t', q);
                f.push_back(line.substr(q, t == std::string::npos ? std::string::npos : t - q));
                if (t == std::string::npos) break;
                q = t + 1;
            }
            if (f.size() < 5) continue;
            Item it;
            it.row = std::atoi(f[0].c_str());
            it.key = static_cast<uint32_t>(std::strtoul(f[1].c_str(), nullptr, 10));
            it.stringKey = f[2];
            it.equipType = static_cast<uint32_t>(std::strtoul(f[3].c_str(), nullptr, 10));
            it.name = f[4];
            g_byRow[it.row] = g_items.size();
            g_items.push_back(std::move(it));
        }
        LOG("[db] %zu dyeable items (%s)", g_items.size(), fromFile ? "file next to the plugin" : "built into the plugin");
        return !g_items.empty();
    }

    int Count() { return static_cast<int>(g_items.size()); }

    const Item* ByRow(int row)
    {
        const auto it = g_byRow.find(row);
        return it == g_byRow.end() ? nullptr : &g_items[it->second];
    }
}
