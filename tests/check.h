// lapse-tui - a terminal interface for the LAPSE subtitle sync engine
// Copyright (C) 2026 Rasmus Stisen Jensen (rs-jensen)
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <iostream>

inline int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #cond << '\n'; \
            failures++;                                                          \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        auto got_ = (a);                                                                   \
        auto want_ = (b);                                                                  \
        if (!(got_ == want_)) {                                                            \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " << #a << " is " << got_     \
                      << ", not " << want_ << '\n';                                        \
            failures++;                                                                    \
        }                                                                                  \
    } while (0)
