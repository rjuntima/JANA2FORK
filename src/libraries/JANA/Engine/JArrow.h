
// Copyright 2020, Jefferson Science Associates, LLC.
// Subject to the terms in the LICENSE file found in the top-level directory.


#ifndef GREENFIELD_ARROW_H
#define GREENFIELD_ARROW_H

#include <iostream>
#include <cassert>
#include <vector>

#include "JArrowMetrics.h"
#include <JANA/JLogger.h>

class JArrow {

public:
    enum class State { Unopened, Running, Paused, Finished };
    enum class NodeType {Source, Sink, Stage, Group};
    enum class BackoffStrategy { Constant, Linear, Exponential };
    using duration_t = std::chrono::steady_clock::duration;

private:
    // Info
    const std::string m_name;     // Used for human understanding
    const bool m_is_parallel;     // Whether or not it is safe to parallelize
    const NodeType m_type;
    JArrowMetrics m_metrics;      // Performance information accumulated over all workers

    // Knobs
    size_t m_chunksize = 1;       // Number of items to pop off the input queue at once
    BackoffStrategy m_backoff_strategy = BackoffStrategy::Exponential;
    duration_t m_initial_backoff_time = std::chrono::microseconds(1);
    duration_t m_checkin_time = std::chrono::milliseconds(500);
    unsigned m_backoff_tries = 4;

    JLogger m_logger {JLogger::Level::DEBUG};
    mutable std::mutex m_mutex;  // Protects access to arrow properties.


    // Scheduler stats
    // These are protected by the Topology mutex, NOT the Arrow mutex!!!
    State m_state = State::Unopened;
    int64_t m_thread_count = 0;            // Current number of threads assigned to this arrow
    int64_t m_running_upstreams = 0;       // Current number of running arrows immediately upstream
    int64_t* m_running_arrows = nullptr;   // Current number of running arrows total, so we can detect pauses
    std::vector<JArrow *> m_listeners;     // Downstream Arrows
public:

    // Constants

    bool is_parallel() { return m_is_parallel; }

    std::string get_name() { return m_name; }


    // Written externally

    void set_logger(JLogger logger) {
        m_logger = logger;
    }

    void set_chunksize(size_t chunksize) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_chunksize = chunksize;
    }

    size_t get_chunksize() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_chunksize;
    }

    void set_backoff_tries(unsigned backoff_tries) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_backoff_tries = backoff_tries;
    }

    unsigned get_backoff_tries() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_backoff_tries;
    }

    BackoffStrategy get_backoff_strategy() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_backoff_strategy;
    }

    void set_backoff_strategy(BackoffStrategy backoff_strategy) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_backoff_strategy = backoff_strategy;
    }

    duration_t get_initial_backoff_time() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_initial_backoff_time;
    }

    void set_initial_backoff_time(const duration_t& initial_backoff_time) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_initial_backoff_time = initial_backoff_time;
    }

    const duration_t& get_checkin_time() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_checkin_time;
    }

    void set_checkin_time(const duration_t& checkin_time) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_checkin_time = checkin_time;
    }

    void update_thread_count(int thread_count_delta) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_thread_count += thread_count_delta;
    }

    size_t get_thread_count() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_thread_count;
    }

    // TODO: Metrics should be encapsulated so that only actions are to update, clear, or summarize
    JArrowMetrics& get_metrics() {
        return m_metrics;
    }

    NodeType get_type() {
        return m_type;
    }

    JArrow(std::string name, bool is_parallel, NodeType arrow_type, size_t chunksize=16) :
            m_name(std::move(name)), m_is_parallel(is_parallel), m_type(arrow_type), m_chunksize(chunksize) {

        m_metrics.clear();
    };

    virtual ~JArrow() = default;

    virtual void initialize() {};

    virtual void execute(JArrowMetrics& result, size_t location_id) = 0;

    virtual void finalize() {};

    virtual size_t get_pending() { return 0; }

    virtual size_t get_threshold() { return 0; }

    virtual void set_threshold(size_t /* threshold */) {}




    State get_state() const {
        return m_state;
    }

    int64_t get_running_upstreams() const {
        return m_running_upstreams;
    }

    void set_running_arrows(int64_t* running_arrows_ptr) {
        m_running_arrows = running_arrows_ptr;
    }

    void run() {
        LOG_INFO(m_logger) << "Arrow '" << m_name << "' running (previous state: " << m_state << ")" << LOG_END;
        if (m_state == State::Running || m_state == State::Finished) return;
        State old_status = m_state;
        if (m_running_arrows != nullptr) (*m_running_arrows)++;
        for (auto listener: m_listeners) {
            listener->m_running_upstreams++;
            listener->run();  // Activating something recursively activates everything downstream.
        }
        if (old_status == State::Unopened) {
            LOG_DEBUG(m_logger) << "Arrow '" << m_name << "': Initializing" << LOG_END;
            initialize();
        }
        m_state = State::Running;
    }

    void pause() {
        LOG_INFO(m_logger) << "Arrow '" << m_name << "' pausing. (previous state: " << m_state << ")" << LOG_END;
        if (m_state != State::Running) return; // pause() is a no-op unless running
        if (m_running_arrows != nullptr) (*m_running_arrows)--;
        for (auto listener: m_listeners) {
            listener->m_running_upstreams--;
            // listener->pause();
            // This is NOT a sufficient condition for pausing downstream listeners.
            // What we need is zero running upstreams AND zero messages in queue AND zero threads currently processing
            // Correspondingly, the scheduler or worker needs to be the one to call pause() when this condition is reached.
        }
        m_state = State::Paused;
    }

    void finish() {
        LOG_INFO(m_logger) << "Arrow '" << m_name << "' finishing. (previous state: " << m_state << ")" << LOG_END;
        State old_status = m_state;
        if (old_status == State::Unopened) {
            LOG_DEBUG(m_logger) << "Arrow '" << m_name << "': Initializing (belatedly)" << LOG_END;
            initialize();
        }
        if (old_status == State::Running) {
            if (m_running_arrows != nullptr) (*m_running_arrows)--;
            for (auto listener: m_listeners) {
                listener->m_running_upstreams--;
            }
        }
        if (old_status != State::Finished) {
            LOG_DEBUG(m_logger) << "Arrow '" << m_name << "': Finalizing" << LOG_END;
            this->finalize();
        }
        m_state = State::Finished;
    }

    void attach(JArrow* downstream) {
        m_listeners.push_back(downstream);
    };

};


inline std::ostream& operator<<(std::ostream& os, const JArrow::NodeType& nt) {
    switch (nt) {
        case JArrow::NodeType::Stage: os << "Stage"; break;
        case JArrow::NodeType::Source: os << "Source"; break;
        case JArrow::NodeType::Sink: os << "Sink"; break;
        case JArrow::NodeType::Group: os << "Group"; break;
    }
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const JArrow::State& s) {
    switch (s) {
        case JArrow::State::Unopened: os << "Unopened"; break;
        case JArrow::State::Running:  os << "Running"; break;
        case JArrow::State::Paused: os << "Stopped"; break;
        case JArrow::State::Finished: os << "Finished"; break;
    }
    return os;
}

#endif // GREENFIELD_ARROW_H
