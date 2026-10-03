#pragma once
// Four-state logic values for the built-in simulator (PLAN.md 6.1).
#include <cstdint>

namespace chiply::sim {

enum class V : std::uint8_t { L = 0, H = 1, X = 2, Z = 3 };

inline char toChar(V v) { return "01xz"[int(v)]; }
inline bool known(V v) { return v == V::L || v == V::H; }
inline V fromBool(bool b) { return b ? V::H : V::L; }

// Inputs: Z behaves like X (an undriven input is unknown).
inline V in(V v) { return v == V::Z ? V::X : v; }

inline V vnot(V a)
{
    a = in(a);
    return a == V::X ? V::X : fromBool(a == V::L);
}
inline V vand(V a, V b)
{
    a = in(a), b = in(b);
    if (a == V::L || b == V::L) return V::L;
    if (a == V::H && b == V::H) return V::H;
    return V::X;
}
inline V vor(V a, V b)
{
    a = in(a), b = in(b);
    if (a == V::H || b == V::H) return V::H;
    if (a == V::L && b == V::L) return V::L;
    return V::X;
}
inline V vxor(V a, V b)
{
    a = in(a), b = in(b);
    if (a == V::X || b == V::X) return V::X;
    return fromBool(a != b);
}
inline V vmux(V a, V b, V sel)
{
    sel = in(sel);
    if (sel == V::L) return in(a);
    if (sel == V::H) return in(b);
    a = in(a), b = in(b);
    return (a == b && a != V::X) ? a : V::X;
}

} // namespace chiply::sim
