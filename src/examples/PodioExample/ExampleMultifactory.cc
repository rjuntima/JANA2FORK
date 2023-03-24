
// Copyright 2023, Jefferson Science Associates, LLC.
// Subject to the terms in the LICENSE file found in the top-level directory.


#include "ExampleMultifactory.h"
#include <JANA/JEvent.h>
#include "DatamodelGlue.h"

ExampleMultifactory::ExampleMultifactory() {

    // We need to declare upfront what our Multifactory produces.
    DeclarePodioOutput<ExampleHit>("hits_filtered", false);
    DeclarePodioOutput<ExampleCluster>("clusters_from_hits_filtered");
    DeclarePodioOutput<ExampleCluster>("clusters_filtered", false);

    // TODO: Discuss tag vs collection names
}

void ExampleMultifactory::Process(const std::shared_ptr<const JEvent> & event) {

    // Using the classic JANA event->Get<T>() and SetData<T>
    // Note that these still manage PODIO collections correctly under the hood, yet aren't restricted to PODIO types.
    // If you want to manipulate PODIO collections directly, see further down.
    auto hits = event->Get<ExampleHit>("hits");

    // We are going to write back a collection of hits that are simply a filtered version of the other ones
    // These are already owned by the 'hits' collection, and we are smart enough to not have to duplicate them.
    std::vector<ExampleHit*> hits_filtered;
    for (auto* hit : hits) {
        if (hit->energy() > 1.0) {
            hits_filtered.push_back(const_cast<ExampleHit*>(hit));
            // TODO: Do something about the const cast
        }
    }
    SetData<ExampleHit>("hits_filtered", hits_filtered);


    // If we have created PODIO objects that DON'T belong to a collection already, it will create the collection.
    // Note that references still work.
    std::vector<ExampleCluster*> clusters_from_hits_filtered;

    MutableExampleCluster quadrant1, quadrant2, quadrant3, quadrant4;

    for (auto* hit : hits) {
        if (hit->x() > 0 && hit->y() > 0) {
            quadrant1.addHits(*hit);
            quadrant1.energy(quadrant1.energy() + hit->energy());
        }
        else if (hit->x() > 0 && hit->y() <= 0) {
            quadrant4.addHits(*hit);
            quadrant4.energy(quadrant4.energy() + hit->energy());
        }
        else if (hit->x() <= 0 && hit->y() > 0) {
            quadrant2.addHits(*hit);
            quadrant2.energy(quadrant2.energy() + hit->energy());
        }
        else if (hit->x() <= 0 && hit->y() <= 0) {
            quadrant3.addHits(*hit);
            quadrant3.energy(quadrant3.energy() + hit->energy());
        }
    }

    if (quadrant1.Hits_size() > 0) clusters_from_hits_filtered.push_back(new ExampleCluster(quadrant1));
    if (quadrant2.Hits_size() > 0) clusters_from_hits_filtered.push_back(new ExampleCluster(quadrant2));
    if (quadrant3.Hits_size() > 0) clusters_from_hits_filtered.push_back(new ExampleCluster(quadrant3));
    if (quadrant4.Hits_size() > 0) clusters_from_hits_filtered.push_back(new ExampleCluster(quadrant4));
    // TODO: Add support for calling SetData with mutable PODIO objects

    SetData<ExampleCluster>("clusters_from_hits_filtered", clusters_from_hits_filtered);


    // In this section we use the PODIO-specific event->GetCollection<T> and SetCollection<T>.
    // These are useful when you are interacting with algorithms
    auto clusters = event->GetCollection<ExampleCluster>("clusters");
    auto clusters_filtered = new ExampleClusterCollection;
    // TODO: Unique pointer, raw pointer, or value?

    for (auto cluster : *clusters) {
        if (cluster.energy() > 5.0) {
            clusters_filtered->push_back(cluster);
        }
    }
    SetCollection<ExampleCluster>("clusters_filtered", clusters_filtered);

}
