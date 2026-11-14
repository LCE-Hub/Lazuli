#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace lazuli {
bool rasterize_svg(const std::string &svg, std::vector<unsigned char> &rgba,
                   int &width, int &height);
} // namespace lazuli
