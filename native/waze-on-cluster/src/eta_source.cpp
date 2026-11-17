// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Exanthiax
#include "eta_source.h"

#include <dlfcn.h>

#include <set>
#include <vsomeip/vsomeip.hpp>

#include "eta.h"
#include "log.h"


namespace woc {

namespace {

const char kRuntimeGet[] = "_ZN10vsomeip_v37runtime3getEv";   /* vsomeip::runtime::get() */
const char kAppName[] = "waze_on_cluster_eta";

typedef std::shared_ptr<vsomeip::runtime> (*RuntimeGet)();

}  // namespace

EtaSource::~EtaSource() { stop(); }

bool EtaSource::start(const std::string &lib, std::string *why)
{
    if (app_)
        return true;
    if (!lib_)
        lib_ = dlopen(lib.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib_) {
        const char *e = dlerror();
        *why = std::string("cannot load ") + lib + ": " + (e ? e : "unknown error");
        return false;
    }
    RuntimeGet get = (RuntimeGet)dlsym(lib_, kRuntimeGet);
    if (!get) {
        *why = lib + " has no vsomeip::runtime::get()";
        return false;
    }
    std::shared_ptr<vsomeip::runtime> rt = get();
    std::shared_ptr<vsomeip::application> app = rt ? rt->create_application(kAppName) : nullptr;
    if (!app || !app->init()) {
        *why = "the vsomeip application did not start";
        return false;
    }
    app->register_availability_handler(
        kGuidanceService, kGuidanceInstance,
        [this](vsomeip::service_t, vsomeip::instance_t, bool up) { set_available(up); }, kGuidanceMajor,
        vsomeip::ANY_MINOR);
    app->register_message_handler(kGuidanceService, kGuidanceInstance, kDestinationEvent,
                                  [this](const std::shared_ptr<vsomeip::message> &m) {
                                      std::shared_ptr<vsomeip::payload> p = m->get_payload();
                                      if (p)
                                          deliver(p->get_data(), p->get_length());
                                  });
    app->request_service(kGuidanceService, kGuidanceInstance, kGuidanceMajor, vsomeip::ANY_MINOR);
    const std::set<vsomeip::eventgroup_t> groups{kDestinationGroup};
    app->request_event(kGuidanceService, kGuidanceInstance, kDestinationEvent, groups,
                       vsomeip::event_type_e::ET_FIELD);
    app->subscribe(kGuidanceService, kGuidanceInstance, kDestinationGroup, kGuidanceMajor);
    app_ = app;
    thread_ = std::thread([app] { app->start(); });
    log_info("eta: subscribed to the navigation's destination (service 0x%04x.%04x v%u, event 0x%04x)",
             kGuidanceService, kGuidanceInstance, kGuidanceMajor, kDestinationEvent);
    return true;
}

void EtaSource::stop()
{
    if (!app_)
        return;
    std::shared_ptr<vsomeip::application> app = std::static_pointer_cast<vsomeip::application>(app_);
    app->clear_all_handler();
    app->stop();
    if (thread_.joinable())
        thread_.join();
    app_.reset();
    /* lib_ stays loaded: vsomeip's static objects outlive the application */
}

bool EtaSource::take(std::vector<uint8_t> *payload)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!fresh_)
        return false;
    fresh_ = false;
    *payload = newest_;
    return true;
}

bool EtaSource::available() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return available_;
}

void EtaSource::deliver(const uint8_t *p, size_t n)
{
    std::lock_guard<std::mutex> lock(mutex_);
    newest_.assign(p, p + n);
    fresh_ = true;
}

void EtaSource::set_available(bool up)
{
    std::lock_guard<std::mutex> lock(mutex_);
    available_ = up;
}

}  // namespace woc
