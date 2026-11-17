// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Exanthiax
/* The route's arrival time, time left and distance left, read from the head
 * unit's navigation Guidance service, and the text the cluster shows for
 * them (eta_strip.h draws it).
 *
 * The phone's route guidance (CarPlay's iAP2 RouteGuidanceUpdate, Android
 * Auto's and CarLife's too) goes from carplay (aapd, carlife) to
 * NavController's de.bmw.infotainment.navigation.projectedmodeguiding.
 * ProjectedModeGuiding, which publishes it, as it does BMW's own route, in
 * de.bmw.infotainment.navigation.guiding.Guidance: SOME/IP service 0xfb1a,
 * instance 0x80, interface version 7 (VERIFIED: the unit's guiding proxy
 * library registers it so). Each field of Guidance is event 0x8000 + n in
 * eventgroup n (VERIFIED from the proxy's deployment). Event 0x8004 (and
 * 0x8005, the same bytes) is the destination (GuidingTypes::DestinationInfo,
 * big-endian, VERIFIED against an iPhone's Apple Maps route on the car,
 * carplay 22.28.3, parked):
 *
 *   offset  0  u32  0
 *           4  u32  0
 *           8  u32  time left, seconds          (240: the phone's "4 min")
 *          12  ...  0
 *          18  u16  arrival year, u8 month, u8 day, u8 0, u8 hour, u8 minute,
 *                   u8 1                         (the route's start + time left:
 *                                                 not moved on while parked, so
 *                                                 not used; see eta_text)
 *          26  string: u32 length (BOM and NUL included), UTF-8 with BOM, NUL
 *                   distance left as the phone shows it   ("0.6")
 *           +  u32  its unit                     (3: miles, the phone's "mi")
 *           +  f64  distance left, metres        (965)
 *           ...     the rest, not used
 *
 * Without a route the field still comes, with an empty distance text and
 * numbers left over (VERIFIED on the car): a route needs the text and some
 * time or distance left. Waze sends the car no route guidance, so with
 * Waze there is none (cluster-render.conf, guide hold). INFERRED: the
 * other unit values (kilometres, metres, feet, yards) and how a route over
 * an hour looks. A unit other than miles shows the metres as km or m. */
#ifndef WOC_ETA_H
#define WOC_ETA_H

#include <stdint.h>
#include <time.h>
#include <string>
#include <vector>

namespace woc {

const uint16_t kGuidanceService = 0xfb1a;
const uint16_t kGuidanceInstance = 0x80;
const uint8_t kGuidanceMajor = 7;
const uint16_t kDestinationEvent = 0x8004;
const uint16_t kDestinationGroup = 4;

/* Distance units in DestinationInfo. */
const uint32_t kUnitMiles = 3;   /* VERIFIED */

struct Eta {
    bool route = false;        /* a route is being guided: some time or distance left */
    uint32_t left_s = 0;       /* time left */
    std::string distance;      /* distance left as the phone shows it, without its unit */
    uint32_t unit = 0;
    double metres = 0;
};

/* A DestinationInfo payload (event 0x8004); false for one too short or not
 * shaped as above, *out untouched. */
bool decode_destination(const uint8_t *p, size_t n, Eta *out);
inline bool decode_destination(const std::vector<uint8_t> &p, Eta *out)
{
    return decode_destination(p.data(), p.size(), out);
}

/* "13:22 · 4 min · 0.6 mi": the arrival as now + time left in the head
 * unit's local time (clock12: "1:22 pm"), the time left, the distance left;
 * empty without a route. */
std::string eta_text(const Eta &e, time_t now, bool clock12);

}  // namespace woc

#endif
