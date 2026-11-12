#include "Version.h"

#include <cctype>
#include <vector>

namespace peak
{
namespace
{
std::vector<int> parts (const std::string& v)
{
    std::vector<int> out;
    size_t i = (! v.empty() && (v[0] == 'v' || v[0] == 'V')) ? 1 : 0;
    while (i < v.size() && std::isdigit ((unsigned char) v[i]))
    {
        int n = 0;
        while (i < v.size() && std::isdigit ((unsigned char) v[i]))
            n = n * 10 + (v[i++] - '0');
        out.push_back (n);
        if (i < v.size() && v[i] == '.')
            ++i;
        else
            break;
    }
    return out;
}
} // namespace

bool isNewerVersion (const std::string& latest, const std::string& current)
{
    const auto a = parts (latest), b = parts (current);
    if (a.empty())
        return false;
    for (size_t i = 0; i < std::max (a.size(), b.size()); ++i)
    {
        const int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y)
            return x > y;
    }
    return false;
}
} // namespace peak
