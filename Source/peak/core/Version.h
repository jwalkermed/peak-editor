#pragma once

#include <string>

namespace peak
{
// Whether release `latest` (e.g. "v1.3.0", from a GitHub release tag) is newer than `current` ("1.2.0").
// Numbers are compared part by part; a leading "v" and anything after the numbers ("-beta") are ignored.
bool isNewerVersion (const std::string& latest, const std::string& current);
} // namespace peak
