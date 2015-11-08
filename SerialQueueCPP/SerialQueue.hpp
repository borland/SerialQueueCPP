//
//  SerialQueue.hpp
//  SerialQueueCPP
//
//  Created by Orion Edwards on 1/11/15.
//  Copyright © 2015 Orion Edwards. All rights reserved.
//

#ifndef SerialQueue_hpp
#define SerialQueue_hpp

#include "Interfaces.hpp"
#include <queue>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <sys/syscall.h>

// this is a nasty hack because xcode doesn't have thread_local
// it's entirely not needed on windows or linux
// it's only here so I can develop on my mac (which doesn't need SerialQueue anyway, just use GCD)
template<typename T> class thread_local_value {
    std::unordered_map<uint64_t, T> m_storage;
    std::mutex m_mtx;
public:
    T* operator->() {
        return get();
    };
    
    T* get() {
        std::lock_guard<std::mutex> guard(m_mtx);
        return &m_storage[SYS_thread_selfid];
    }
    
    void clear() {
        std::lock_guard<std::mutex> guard(m_mtx);
        m_storage.erase(SYS_thread_selfid);
    }
};

class SerialQueueImpl :
public IDispatchQueue,
public IDisposable,
public std::enable_shared_from_this<SerialQueueImpl> {
protected:
    typedef std::function<void()> Action;
        
    enum class AsyncState {
        Idle = 0,
        Scheduled,
        Processing
    };

    static thread_local_value<std::deque<SerialQueueImpl*>> s_queueStack;
    
    const std::shared_ptr<IThreadPool> m_threadPool;
    
    // lock-order: We must never hold both these locks concurrently
    std::mutex m_schedulerLock; // acquire this before adding any async/timer actions
    std::mutex m_executionLock; // acquire this before doing dispatchSync
    
    std::vector<Action> m_asyncActions; // aqcuire m_schedulerLock
    std::unordered_set<IDisposable> m_timers; // acquire m_schedulerLock
    volatile AsyncState m_asyncState = AsyncState::Idle; // acquire m_schedulerLock
    bool m_isDisposed = false; // acquire m_schedulerLock

    /// <summary>Internal function which runs on the threadpool to execute the actual async actions</summary>
    virtual void ProcessAsync();
    
public:
    
    /// <summary>Constructs a new SerialQueue backed by the given ThreadPool</summary>
    /// <param name="threadpool">The threadpool to queue async actions to</param>
    SerialQueueImpl(std::shared_ptr<IThreadPool> threadpool);
    
    /// <summary>Constructs a new SerialQueue backed by the default TaskThreadPool</summary>
    SerialQueueImpl();
    
    /// <summary>This event is raised whenever an asynchronous function (via DispatchAsync or DispatchAfter)
    /// throws an unhandled exception</summary>
// TODO
//    public event EventHandler<UnhandledExceptionEventArgs> UnhandledException;
    
    /// <summary>Checks whether the currently-executing function is
    /// on this queue, and throw an OperationInvalidException if it is not</summary>
    void VerifyQueue() override;
    
    /// <summary>Schedules the given action to run asynchronously on the queue when it is available</summary>
    /// <param name="action">The function to run</param>
    /// <returns>A disposable token which you can use to cancel the async action if it has not run yet.
    /// It is always safe to dispose this token, even if the async action has already run</returns>
    IDisposable DispatchAsync(Action action) override;
    
    /// <summary>Runs the given action on the queue.
    /// Blocks until the action is fully complete.
    /// This implementation will not switch threads to run the function</summary>
    /// <param name="action">The function to run.</param>
    void DispatchSync(Action action) override;
    
    /// <summary>Shuts down the queue. All unstarted async actions will be dropped,
    /// and any future attempts to call one of the Dispatch functions will throw an
    /// ObjectDisposedException</summary>
    void Dispose() override;
};

// ref-counting wrapper object so you can treat a SerialQueue as a pass-by-value object
class SerialQueue : public IDispatchQueue {
    std::shared_ptr<SerialQueueImpl> m_sptr;
    
public:
    SerialQueue();
    // copy, move, assignment constructors and operators all implicitly defined
};


#endif /* SerialQueue_hpp */
