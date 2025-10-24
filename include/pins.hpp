#pragma once
#include <array>
using namespace std;

namespace Pins {
    inline constexpr int rows = 3;
    inline constexpr int cols = 3;

    inline constexpr array<array<int, 3>, 3> BUTTONS = {
        {
            {34, 35, 32},
            {33, 25, 26},
            {27, 14, 12}
        }
    };

    inline constexpr array<array<int, 3>, 3> LEDS = {
        {
            {23, 22, 21},
            {19, 18,  5},
            {17, 16,  4}
        }
    };
    
    inline int ledPos(int r, int c) {
        return LEDS[r][c];
    }

    inline int buttonPos(int idx1, int& r, int & c) {
        --idx1;
        r = idx1 / cols;
        c = idx1 % cols;
    }
}
