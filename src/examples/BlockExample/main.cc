
// Copyright 2020, Jefferson Science Associates, LLC.
// Subject to the terms in the LICENSE file found in the top-level directory.

#include <JANA/JApplication.h>
#include <JANA/Engine/JTopologyBuilder.h>
#include <JANA/Engine/JBlockSourceArrow.h>
#include <JANA/Engine/JBlockDisentanglerArrow.h>
#include <JANA/Engine/JEventProcessorArrow.h>

#include "BlockExampleSource.h"
#include "BlockExampleProcessor.h"

std::shared_ptr<JArrowTopology> configure_block_topology(std::shared_ptr<JArrowTopology> topology) {

    auto source = new BlockExampleSource;
    auto processor = new BlockExampleProcessor;

    auto block_queue = new JMailbox<MyBlock*>;
    auto event_queue = new JMailbox<std::shared_ptr<JEvent>>;

    // topology->queues.push_back(block_queue);
    // FIXME: block_queue is a (very minor) memory leak
    topology->queues.push_back(event_queue);

    auto block_source_arrow = new JBlockSourceArrow<MyBlock>("block_source", source, block_queue);
    auto block_disentangler_arrow = new JBlockDisentanglerArrow<MyBlock>("block_disentangler", source, block_queue, event_queue, topology->event_pool);
    auto processor_arrow = new JEventProcessorArrow("processors", event_queue, nullptr, topology->event_pool);

    processor_arrow->add_processor(processor);

    topology->arrows.push_back(block_source_arrow);
    topology->arrows.push_back(block_disentangler_arrow);
    topology->arrows.push_back(processor_arrow);

    topology->sources.push_back(block_source_arrow);
    topology->sinks.push_back(processor_arrow);

    block_source_arrow->attach(block_disentangler_arrow);
    block_disentangler_arrow->attach(processor_arrow);

    block_source_arrow->set_running_arrows(&topology->running_arrow_count);
    block_disentangler_arrow->set_running_arrows(&topology->running_arrow_count);
    processor_arrow->set_running_arrows(&topology->running_arrow_count);

    // If you want to add additional processors loaded from plugins, do this like so:
    for (auto proc : topology->component_manager->get_evt_procs()) {
        processor_arrow->add_processor(proc);
    }

    // If you want to add additional sources loaded from plugins, you'll also need to set up a JEventSourceArrow.
    // Look at JTopologyBuilder::create_default_topology() for an example.

    return topology;
}

int main() {
    // auto parms = new JParameterManager;
    // parms->SetParameter("log:trace", "JWorker,JScheduler,JArrowProcessingController,JArrowTopology");
    JApplication app; //(parms);
    app.SetParameterValue("jana:limit_total_events_in_flight", false);  // Otherwise we run out and segfault!!!
    app.SetParameterValue("nthreads", 3); // So that we see some interesting ordering in our log
    app.GetService<JTopologyBuilder>()->set_configure_fn(configure_block_topology);
    app.Run(true);
}

