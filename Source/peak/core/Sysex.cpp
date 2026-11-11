#include "Sysex.h"

#include <algorithm>
#include <cctype>

namespace peak
{
namespace
{
Bytes dumpRequest (uint8_t command, int bank, int slot)
{
    return { 0xF0, 0x00, 0x20, 0x29, 0x01, 0x10, 0x00, 0x7E, command, 0x00, 0x00, 0x00,
             static_cast<uint8_t> (bank & 0x7F), static_cast<uint8_t> (slot & 0x7F), 0xF7 };
}
} // namespace

Bytes currentSoundRequest() { return dumpRequest (0x40, 0, 0); }
Bytes slotRequest (int bank, int slot) { return dumpRequest (0x41, bank + 1, slot); }
Bytes settingsRequest() { return dumpRequest (0x44, 0, 0); }

Bytes wavetableNameRequest (int index)
{
    return { 0xF0, 0x00, 0x20, 0x29, 0x01, 0x10, 0x00, 0x7E, 0x47, 0x00, 0x00, static_cast<uint8_t> (index & 0x7F), 0x00, 0x00, 0xF7 };
}

bool isWavetableName (const uint8_t* d, size_t n)
{
    return n >= 17 && n <= 40 && d[0] == 0xF0 && d[1] == 0x00 && d[2] == 0x20 && d[3] == 0x29 && d[4] == 0x01 && d[5] == 0x10
        && d[8] == 0x07 && d[n - 1] == 0xF7;
}

std::optional<std::pair<int, std::string>> parseWavetableName (const Bytes& b)
{
    if (! isWavetableName (b.data(), b.size()))
        return std::nullopt;
    std::string name (b.begin() + 15, b.end() - 1);
    while (! name.empty() && name.back() == ' ')
        name.pop_back();
    return std::pair<int, std::string> { b[14], name };
}

std::vector<Bytes> splitSysex (const uint8_t* data, size_t size)
{
    std::vector<Bytes> messages;
    Bytes current;
    bool inMessage = false;

    for (size_t i = 0; i < size; ++i)
    {
        const auto b = data[i];

        if (b == 0xF0)
        {
            current.assign (1, b); // a new F0 abandons any unterminated message
            inMessage = true;
        }
        else if (inMessage)
        {
            current.push_back (b);

            if (b == 0xF7)
            {
                messages.push_back (std::move (current));
                current.clear();
                inMessage = false;
            }
            else if (b & 0x80)
            {
                // Only real-time bytes may be interleaved; anything else aborts the message.
                if (b < 0xF8)
                {
                    current.clear();
                    inMessage = false;
                }
                else
                {
                    current.pop_back();
                }
            }
        }
    }

    return messages;
}

Bytes joinSysex (const std::vector<Bytes>& messages)
{
    Bytes out;
    for (const auto& m : messages)
        out.insert (out.end(), m.begin(), m.end());
    return out;
}

bool isNovationSysex (const uint8_t* data, size_t size)
{
    return size >= 4 && data[0] == 0xF0 && std::equal (std::begin (novationId), std::end (novationId), data + 1);
}

std::string toHex (const uint8_t* data, size_t size, size_t maxBytes)
{
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string s;
    const auto n = std::min (size, maxBytes);
    s.reserve (n * 3 + 4);

    for (size_t i = 0; i < n; ++i)
    {
        if (i > 0)
            s += ' ';
        s += digits[data[i] >> 4];
        s += digits[data[i] & 0x0F];
    }

    if (n < size)
        s += " …";

    return s;
}

std::optional<Bytes> parseHex (std::string_view text)
{
    Bytes out;
    int pending = -1;

    for (const char c : text)
    {
        if (std::isspace (static_cast<unsigned char> (c)) || c == ',')
        {
            if (pending >= 0)
                return std::nullopt; // odd digit count within a token
            continue;
        }

        const auto u = static_cast<unsigned char> (std::toupper (static_cast<unsigned char> (c)));
        int v;
        if (u >= '0' && u <= '9')       v = u - '0';
        else if (u >= 'A' && u <= 'F')  v = u - 'A' + 10;
        else                            return std::nullopt;

        if (pending < 0)
        {
            pending = v;
        }
        else
        {
            out.push_back (static_cast<uint8_t> ((pending << 4) | v));
            pending = -1;
        }
    }

    if (pending >= 0)
        return std::nullopt;

    return out;
}
} // namespace peak
