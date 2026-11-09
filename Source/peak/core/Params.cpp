#include "Params.h"

#include <algorithm>

namespace peak
{
const ParamDef* findParam (std::string_view id)
{
    const auto& all = allParams();
    const auto it = std::find_if (all.begin(), all.end(), [&] (const ParamDef& p) { return id == p.id; });
    return it != all.end() ? &*it : nullptr;
}

int indexOf (const ParamDef& p)
{
    return static_cast<int> (&p - allParams().data());
}
} // namespace peak
