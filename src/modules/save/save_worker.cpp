// Copyright Francesco Di Blasi 2026 - All Rights Reserved

#include "save_worker.hpp"

namespace checkpoint {

bool OperationControl::cancel() {
    State expected = State::Open;
    if (state.compare_exchange_strong(expected, State::Cancelled)) {
        return true;
    }
    return expected == State::Cancelled;
}

bool OperationControl::cancelled() const {
    return state.load() == State::Cancelled;
}

bool OperationControl::begin_commit() {
    State expected = State::Open;
    return state.compare_exchange_strong(expected, State::Committing);
}

void OperationControl::check_cancelled() const {
    if (cancelled()) {
        throw SaveError("Cancelled", "The operation was cancelled.");
    }
}

void OperationControl::finish() {
    // Keep a winning cancellation visible to the result delivery path.
    State expected = State::Open;
    if (!state.compare_exchange_strong(expected, State::Finished)) {
        expected = State::Committing;
        state.compare_exchange_strong(expected, State::Finished);
    }
}

SaveWorker::SaveWorker(std::shared_ptr<Store> backend, std::shared_ptr<Codec> data_codec) :
        store(std::move(backend)), codec(std::move(data_codec)), thread([this] { run(); }) {}

SaveWorker::~SaveWorker() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    ready.notify_one();
    thread.join();
}

void SaveWorker::submit(Job job) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        waiting = std::move(job);
    }
    ready.notify_one();
}

std::vector<Event> SaveWorker::take_events() {
    std::vector<Event> batch;
    std::lock_guard<std::mutex> lock(mutex);
    batch.swap(events);
    return batch;
}

void SaveWorker::post(Event event) {
    std::lock_guard<std::mutex> lock(mutex);
    events.push_back(std::move(event));
}

void SaveWorker::run() {
    for (;;) {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [this] { return stopping || waiting.has_value(); });
        if (stopping) {
            return;
        }

        Job job = std::move(*waiting);
        waiting.reset();
        lock.unlock();
        Outcome result = execute(job);
        post({job.ticket, {}, std::move(result)});
    }
}

Outcome SaveWorker::execute(Job &job) {
    Outcome result;
    try {
        // Check cancellation before forwarding a worker progress phase.
        auto progress = [&](const std::string &phase) {
            job.control->check_cancelled();
            post({job.ticket, phase, std::nullopt});
        };
        job.control->check_cancelled();

        switch (job.kind) {
            case Kind::Save: {
                // Authorize publication once and report the commit phase.
                auto begin_commit = [&] {
                    if (!job.control->begin_commit()) {
                        return false;
                    }
                    post({job.ticket, "Publishing", std::nullopt});
                    return true;
                };
                const Generation generation = write_checkpoint(*store, *codec, job.key,
                        std::move(job.envelope), job.versions, job.limits, begin_commit, progress,
                        job.preserve, result.warning);
                result.value = {{"sequence", std::to_string(generation.sequence)}, {"id", generation.id}};
                break;
            }
            case Kind::Load:
                result.value = read_checkpoint(*store, *codec, job.key, job.selection,
                        job.versions, job.limits, progress);
                if (job.selection) {
                    try {
                        const auto generations = store->list(job.key.profile, job.key.slot);
                        result.historical = generations.empty() || !(generations.front() == *job.selection);
                    } catch (const SaveError &) {
                        // The explicitly selected file was verified, but the head is uncertain.
                        result.historical = true;
                    }
                }
                break;
            case Kind::List:
                progress("Reading");
                result.value = inspect_slot(*store, *codec, job.key, job.versions, job.limits,
                        job.selection, job.page_size, job.structural_only);
                break;
            case Kind::Inspect:
                progress("Reading");
                result.value = inspect_generation(*store, *codec, job.key, *job.selection,
                        job.versions, job.limits, job.structural_only);
                break;
        }
    } catch (const SaveError &error) {
        result.value = error.details;
        result.code = error.code;
        result.message = error.what();
    } catch (const std::exception &) {
        result.value = nullptr;
        result.code = "InternalError";
        result.message = "The persistence operation failed unexpectedly.";
    }

    if (job.kind != Kind::Load || result.code != "Ok") {
        job.control->finish();
    }
    if (job.control->cancelled()) {
        result = {nullptr, "Cancelled", "The operation was cancelled.", {}};
    }
    return result;
}

} // namespace checkpoint
