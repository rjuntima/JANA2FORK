
// Copyright 2023, Jefferson Science Associates, LLC.
// Subject to the terms in the LICENSE file found in the top-level directory.

#include "JFactoryPodioT.h"
#include <JANA/JEvent.h>

podio::Frame* GetOrCreateFrame(const std::shared_ptr<const JEvent>& event) {
    podio::Frame* result = nullptr;
    try {
        result = const_cast<podio::Frame*>(event->GetSingle<podio::Frame>(""));
    }
    catch (...) {
        result = new podio::Frame;
        event->Insert(result);
    }
    return result;
}