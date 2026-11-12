// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#pragma once

#include "save_core.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace checkpoint {

// Worker request types used for dispatch and completion routing.
enum class Kind { Save, Load, List, Inspect };

// Report whether an operation needs live gameplay bindings.
inline bool requires_world(Kind kind) {
    return kind == Kind::Save || kind == Kind::Load;
}

// Thread-safe cancellation and commit state shared with the worker.
class OperationControl {
public:
    // Try to win cancellation before an operation starts committing.
    bool cancel();
    // Report whether cancellation won the operation race.
    bool cancelled() const;
    // Acquire the commit boundary while the operation remains open.
    bool begin_commit();
    // Raise a checkpoint cancellation error if cancellation has won.
    void check_cancelled() const;
    // Mark a completed operation while preserving a winning cancellation.
    void finish();

private:
    // Mutually exclusive states for cancellation and commit authorization.
    enum class State { Open, Cancelled, Committing, Finished };
    // Atomic operation state shared between the coordinator and worker.
    std::atomic<State> state{State::Open};
};

// Detached checkpoint request passed to the worker.
struct Job {
    // Coordinator ticket associated with the job.
    int64_t ticket = 0;
    // Operation executed by the worker.
    Kind kind = Kind::Save;
    // Profile and slot addressed by the operation.
    SlotKey key;
    // Explicit generation or page cursor supplied by the requester.
    std::optional<Generation> selection;
    // Generation protected by a successful restore of this slot.
    std::optional<Generation> preserve;
    // Maximum number of generations requested by a listing.
    size_t page_size = 16;
    // Use structural inspection when no participant schema is declared.
    bool structural_only = false;
    // Leave the restored candidate detached for explicit activation.
    bool prepare_only = false;
    // Metadata before save capture and the detached checkpoint after capture.
    Json envelope;
    // App, scope, and participant versions copied at request admission.
    Versions versions;
    // Resource budgets copied at request admission.
    Limits limits;
    // Cancellation and commit state shared with the coordinator.
    std::shared_ptr<OperationControl> control;
};

// Terminal worker result or detached data for main-thread restore.
struct Outcome {
    // Detached checkpoint data, inspected metadata, or published generation identity.
    Json value;
    // Stable result code for the completed operation.
    std::string code = "Ok";
    // Optional diagnostic explaining the result.
    std::string message;
    // Optional diagnostic for a completed operation with a secondary failure.
    std::string warning;
    // Whether an explicitly restored generation may be older than the current head.
    bool historical = false;
};

// Worker notification consumed by the coordinator on the main thread.
struct Event {
    // Ticket that receives this progress or completion event.
    int64_t ticket = 0;
    // Progress phase for a nonterminal notification.
    std::string phase;
    // Result attached to a terminal worker notification.
    std::optional<Outcome> outcome;
};

// Single background worker for detached checkpoint storage and codec work.
class SaveWorker {
public:
    // Start the worker with the supplied storage backend and codec.
    SaveWorker(std::shared_ptr<Store> store, std::shared_ptr<Codec> codec);
    // Signal shutdown and wait for the worker thread to exit.
    ~SaveWorker();
    // Move a detached job into the worker's input slot and wake the thread.
    void submit(Job job);
    // Drain worker events for main-thread delivery.
    std::vector<Event> take_events();

private:
    // Storage backend used by worker jobs.
    std::shared_ptr<Store> store;
    // Codec used by worker jobs.
    std::shared_ptr<Codec> codec;
    // Protects the pending job, shutdown flag, and queued events.
    std::mutex mutex;
    // Wakes the worker when a job arrives or shutdown begins.
    std::condition_variable ready;
    // Requests that the worker exit.
    bool stopping = false;
    // Pending job that the worker has yet to claim.
    std::optional<Job> waiting;
    // Progress and completion notifications awaiting main-thread delivery.
    std::vector<Event> events;
    // Background thread owned and joined by the worker.
    std::thread thread;

    // Wait for jobs, execute them outside the queue lock, and publish results.
    void run();
    // Run a checkpoint operation and translate failures into outcomes.
    Outcome execute(Job &job);
    // Queue a worker event for the coordinator.
    void post(Event event);
};

} // namespace checkpoint
