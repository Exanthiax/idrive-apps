// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Exanthiax
#include "eta.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace woc {

namespace {

uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

double be_double(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = v << 8 | p[i];
    double d;
    std::memcpy(&d, &v, sizeof d);
    return d;
}

const size_t kLeftAt = 8;
const size_t kDistanceAt = 26;
const uint32_t kMaxString = 64;

}  // namespace

bool decode_destination(const uint8_t *p, size_t n, Eta *out)
{
    if (n < kDistanceAt + 4)
        return false;
    const uint32_t len = be32(p + kDistanceAt);
    const size_t s = kDistanceAt + 4;
    /* BOM and NUL at least, then the unit and the metres */
    if (len < 4 || len > kMaxString || n < s + len + 12)
        return false;
    if (p[s] != 0xef || p[s + 1] != 0xbb || p[s + 2] != 0xbf || p[s + len - 1] != 0)
        return false;
    Eta e;
    e.left_s = be32(p + kLeftAt);
    e.distance.assign((const char *)p + s + 3, len - 4);
    for (char c : e.distance)
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e)
            return false;
    e.unit = be32(p + s + len);
    e.metres = be_double(p + s + len + 4);
    if (!std::isfinite(e.metres) || e.metres < 0 || e.metres > 1e8)
        return false;
    /* Without a route the field still comes, its distance text empty and
     * its numbers left over (VERIFIED on the car: "0 min · 0 m" shown):
     * a route needs the phone's distance text and something left. */
    e.route = !e.distance.empty() && (e.left_s >= 30 || e.metres >= 1);
    *out = e;
    return true;
}

std::string eta_text(const Eta &e, time_t now, bool clock12)
{
    if (!e.route)
        return std::string();
    char at[16];
    const time_t arrive = now + (time_t)e.left_s;
    struct tm t;
    localtime_r(&arrive, &t);
    if (clock12)
        std::snprintf(at, sizeof at, "%d:%02d %s", t.tm_hour % 12 ? t.tm_hour % 12 : 12, t.tm_min,
                      t.tm_hour < 12 ? "am" : "pm");
    else
        std::snprintf(at, sizeof at, "%02d:%02d", t.tm_hour, t.tm_min);

    char left[24];
    const uint32_t mins = (e.left_s + 30) / 60;
    if (mins < 60)
        std::snprintf(left, sizeof left, "%u min", mins);
    else if (mins % 60)
        std::snprintf(left, sizeof left, "%u h %u min", mins / 60, mins % 60);
    else
        std::snprintf(left, sizeof left, "%u h", mins / 60);

    char dist[32];
    if (e.unit == kUnitMiles && !e.distance.empty())
        std::snprintf(dist, sizeof dist, "%s mi", e.distance.c_str());
    else if (e.metres >= 1000)
        std::snprintf(dist, sizeof dist, "%.1f km", e.metres / 1000);
    else
        std::snprintf(dist, sizeof dist, "%.0f m", e.metres);

    /* U+00B7 MIDDLE DOT between them */
    return std::string(at) + " \xc2\xb7 " + left + " \xc2\xb7 " + dist;
}

}  // namespace woc
