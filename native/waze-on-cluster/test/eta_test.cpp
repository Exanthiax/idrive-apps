// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Exanthiax
#include "eta.h"
#include "eta_strip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>

using namespace woc;

static int failures = 0;

static void expect(bool ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static std::vector<uint8_t> hex(const char *s)
{
    std::vector<uint8_t> v;
    while (*s) {
        char *end;
        const unsigned long b = strtoul(s, &end, 16);
        if (end == s)
            break;
        v.push_back((uint8_t)b);
        s = end;
    }
    return v;
}

/* Event 0x8004 from the car: an Apple Maps route, 4 min and 0.6 mi left,
 * parked (eta.h). */
static const char kCarPlayRoute[] =
    "00 00 00 00 00 00 00 00 00 00 00 f0 00 00 00 00 00 00 07 ea 0b 11 00 0d 0b 01 00 00 00 07 ef bb bf 30 "
    "2e 36 00 00 00 00 03 40 8e 28 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 "
    "00 04 00 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00";

/* The same with no route: zeros and empty strings, as the other fields of
 * Guidance looked without one. */
static const char kNoRoute[] =
    "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04 ef bb bf 00 "
    "00 00 00 00 00 00 00 00 00 00 00 00";

int main()
{
    setenv("TZ", "UTC", 1);
    tzset();

    Eta e;
    std::vector<uint8_t> p = hex(kCarPlayRoute);
    expect(p.size() == 121, "the captured payload is whole");
    expect(decode_destination(p, &e), "the car's payload decodes");
    expect(e.route && e.left_s == 240, "4 min left");
    expect(e.distance == "0.6" && e.unit == kUnitMiles, "0.6 mi as the phone shows it");
    expect(e.metres == 965.0, "965 m");

    /* 2026-11-17 13:18:00 UTC: the phone said 13:22 */
    const time_t now = 1794921480;
    expect(eta_text(e, now, false) == "13:22 \xc2\xb7 4 min \xc2\xb7 0.6 mi", "24-hour text");
    expect(eta_text(e, now, true) == "1:22 pm \xc2\xb7 4 min \xc2\xb7 0.6 mi", "12-hour text");

    Eta none;
    expect(decode_destination(hex(kNoRoute), &none) && !none.route, "no route decodes as none");
    expect(eta_text(none, now, false).empty(), "no text without a route");

    /* no route, numbers left over but no distance text: none */
    std::vector<uint8_t> left_over = hex(kNoRoute);
    left_over[11] = 12;   /* 12 s */
    Eta stale;
    expect(decode_destination(left_over, &stale) && !stale.route && eta_text(stale, now, false).empty(),
           "no distance text: no route, whatever numbers are left");
    /* the phone's text but nothing left (arrived): none */
    std::vector<uint8_t> arrived = p;
    arrived[11] = 0;
    for (int i = 41; i < 49; i++)
        arrived[i] = 0;
    Eta at;
    expect(decode_destination(arrived, &at) && !at.route, "nothing left: arrived, no route");

    Eta km = e;
    km.unit = 1;
    km.metres = 12345;
    km.left_s = 3 * 3600 + 5 * 60;
    expect(eta_text(km, now, false) == "16:23 \xc2\xb7 3 h 5 min \xc2\xb7 12.3 km", "other units as km, hours");
    km.metres = 400;
    km.left_s = 7200;
    expect(eta_text(km, now, false) == "15:18 \xc2\xb7 2 h \xc2\xb7 400 m", "under a km as m, whole hours");

    Eta keep = e;
    expect(!decode_destination(p.data(), 20, &keep) && keep.left_s == 240, "too short: refused, untouched");
    std::vector<uint8_t> bad = p;
    bad[30] = 0;   /* no BOM */
    expect(!decode_destination(bad, &keep), "a string without its BOM is refused");
    bad = p;
    bad[29] = 0xff;   /* a string longer than the payload */
    expect(!decode_destination(bad, &keep), "a string past the end is refused");

    const std::vector<uint32_t> cps = utf8_codes("2 \xc2\xb7 4\xe2\x80\xaf" "a\xff");
    const std::vector<uint32_t> want = {'2', ' ', 0xb7, ' ', '4', 0x202f, 'a', 0xfffd};
    expect(cps == want, "UTF-8: two and three bytes, a malformed byte as U+FFFD");
    expect(utf8_codes("\xe2\x80").size() == 2, "a cut-off sequence: each byte U+FFFD");

    EtaStrip unloaded;
    std::vector<uint8_t> px;
    expect(!unloaded.draw("13:22", &px), "no drawing before the font loads");

    if (failures) {
        fprintf(stderr, "eta_test: %d failed\n", failures);
        return 1;
    }
    printf("eta_test: ok\n");
    return 0;
}
