// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Exanthiax
/* EtaSource: the route's destination from the head unit's navigation
 * Guidance service (eta.h), subscribed to over SOME/IP.
 *
 * The unit's own libvsomeip3 is loaded at run time (dlopen), so the map
 * does not depend on it: without it, or when its application does not
 * start, there is no arrival time and the map shows as before, with one log
 * line. Only runtime::get() is looked up by name; the rest of vsomeip's
 * interface is virtual. The service runs as hmi_5000, whose vsomeip
 * security policy (/etc/vsomeip/5000_5000) lets it request 0xfb1a
 * (VERIFIED on the car). It only subscribes: it offers nothing and calls no
 * method.
 *
 * vsomeip calls back on its own thread; take() hands the newest payload to
 * the main loop. */
#ifndef WOC_ETA_SOURCE_H
#define WOC_ETA_SOURCE_H

#include <stdint.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace woc {

class EtaSource {
public:
    EtaSource() = default;
    ~EtaSource();
    EtaSource(const EtaSource &) = delete;
    EtaSource &operator=(const EtaSource &) = delete;

    /* Loads lib and starts the subscription; false, with why, when it
     * cannot. */
    bool start(const std::string &lib, std::string *why);
    void stop();
    /* The newest destination payload since the last take(); false when none
     * came. */
    bool take(std::vector<uint8_t> *payload);
    /* The Guidance service is available (from vsomeip). */
    bool available() const;

    /* Called on vsomeip's thread. */
    void deliver(const uint8_t *p, size_t n);
    void set_available(bool up);

private:
    mutable std::mutex mutex_;
    std::vector<uint8_t> newest_;
    bool fresh_ = false;
    bool available_ = false;
    void *lib_ = nullptr;
    std::shared_ptr<void> app_;   /* the vsomeip application, kept opaque here */
    std::thread thread_;
};

}  // namespace woc

#endif
