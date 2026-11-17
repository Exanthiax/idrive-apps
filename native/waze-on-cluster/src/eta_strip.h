// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Exanthiax
/* EtaStrip: the arrival-time text (eta.h) drawn as an RGBA picture: white
 * text in the head unit's own BMW font on a dark, rounded pill, centred in a
 * picture of a fixed size, see-through around the pill. MapFeed shows it as
 * an image layer at the top of the cluster map (map_feed.h, set_badge).
 *
 * The font is rasterised by the unit's own libfreetype, loaded at run time
 * (dlopen) like vsomeip (eta_source.h): without it or the font there is no
 * strip, with one log line, and the map shows as before. The few FreeType
 * calls and the leading members of its public structs it reads are declared
 * here (FreeType 2's ABI, soname 6, has kept them since 2.0; the unit has
 * 2.9.x; eta_strip.cpp). */
#ifndef WOC_ETA_STRIP_H
#define WOC_ETA_STRIP_H

#include <stdint.h>
#include <string>
#include <vector>

namespace woc {

class EtaStrip {
public:
    /* The picture's size: the cluster map's top band holds it (eta_strip.cpp). */
    static const unsigned kWidth = 520, kHeight = 52;

    EtaStrip() = default;
    ~EtaStrip();
    EtaStrip(const EtaStrip &) = delete;
    EtaStrip &operator=(const EtaStrip &) = delete;

    /* Loads lib and the font, its glyphs px pixels high; false, with why,
     * when it cannot. */
    bool load(const std::string &lib, const std::string &font, unsigned px, std::string *why);
    /* text (UTF-8) drawn into *rgba, kWidth x kHeight, straight alpha; empty
     * text: all see-through. False before load(). Text wider than the picture
     * is cut at its edges. */
    bool draw(const std::string &text, std::vector<uint8_t> *rgba);

private:
    struct Ft;
    Ft *ft_ = nullptr;
};

/* The code points of UTF-8 text; a malformed byte counts as U+FFFD. */
std::vector<uint32_t> utf8_codes(const std::string &s);

}  // namespace woc

#endif
