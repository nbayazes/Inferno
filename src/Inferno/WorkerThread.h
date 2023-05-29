#pragma once

#include <thread>
#include <mutex>
#include <spdlog/spdlog.h>

// A long running worker thread that waits for notifications to start processing
class WorkerThread {
    std::mutex _notifyLock;
    std::condition_variable _workAvailable;
    std::thread _worker;
    std::atomic<bool> _hasWork;
    std::atomic<bool> _alive;
    std::string _name;

public:
    WorkerThread(std::string_view name) : _name(name) {}
    virtual ~WorkerThread() { Stop(); }

    void Start() {
        assert(!_alive);
        _alive = true;
        _worker = std::thread(&WorkerThread::Worker, this);
    }

    void Stop() {
        assert(_alive);
        _alive = false;
        _workAvailable.notify_all();
        if (_worker.joinable())
            _worker.join();
    }

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread(WorkerThread&&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;
    WorkerThread& operator=(WorkerThread&&) = delete;

    // Wake up the worker
    void Notify() {
        _hasWork = true;
        _workAvailable.notify_one();
    }

protected:
    virtual void Work() = 0;
    bool HasWork() { return _hasWork; }

private:
    void Worker() {
        SPDLOG_INFO("Starting worker `{}`", _name);
        while (_alive) {
            try {
                _hasWork = false;
                Work();

                // New work could be requested while work is being done, so check before sleeping
                if (!_hasWork) {
                    std::unique_lock lock(_notifyLock);
                    _workAvailable.wait(lock); // sleep until work requested
                }
            }
            catch (const std::exception& e) {
                SPDLOG_ERROR(e.what());
            }
        }
        SPDLOG_INFO("Stopping worker `{}`", _name);
    }
};
