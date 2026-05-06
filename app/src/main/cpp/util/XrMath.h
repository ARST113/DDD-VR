#pragma once
#include <array>

namespace xrmath {
inline std::array<float,16> identity() {
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}
}
