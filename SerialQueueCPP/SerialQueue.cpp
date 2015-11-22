//
//  SerialQueue.cpp
//  SerialQueueCPP
//
//  Created by Orion Edwards on 1/11/15.
//  Copyright © 2015 Orion Edwards. All rights reserved.
//

#include "SerialQueue.hpp"
#include <condition_variable>
#include <cassert>
using namespace std;

// C++ has no finally.
template<typename T>
class scope_exit final
{
    T m_action;
public:
    explicit scope_exit(T&& exitScope) : m_action(std::forward<T>(exitScope))
    {}
    
    ~scope_exit()
    { m_action(); }
};

template <typename T>
scope_exit<T> create_scope_exit(T&& exitScope)
{ return scope_exit<T>(std::forward<T>(exitScope)); }

// ----- TaggedAction class -----

atomic<unsigned int> SerialQueueImpl::TaggedAction::s_lastActionTag;

SerialQueueImpl::TaggedAction::TaggedAction(Action act) : tag(++s_lastActionTag), action(std::move(act)) { }

bool SerialQueueImpl::TaggedAction::operator==(const TaggedAction& other) const noexcept {
    return tag == other.tag;
}
bool SerialQueueImpl::TaggedAction::operator!=(const TaggedAction& other) const noexcept {
    return !(*this == other);
}
void SerialQueueImpl::TaggedAction::operator()(){
    action();
}

// ----- SerialQueue wrapper class -----

SerialQueue::SerialQueue(shared_ptr<IThreadPool> threadpool)
    : m_sptr(make_shared<SerialQueueImpl>(threadpool))
{ }

void SerialQueue::DispatchSync(std::function<void ()> action) {
    assert(m_sptr);
    m_sptr->DispatchSync(move(action));
}

IDisposable SerialQueue::DispatchAsync(std::function<void ()> action ) {
    assert(m_sptr);
    return DispatchAsync(move(action));
}

void SerialQueue::VerifyQueue() {
    assert(m_sptr);
    m_sptr->VerifyQueue();
}

// ----- SerialQueueImpl class -----

thread_local_value<std::deque<SerialQueueImpl*>> SerialQueueImpl::s_queueStack;

SerialQueueImpl::SerialQueueImpl(shared_ptr<IThreadPool> threadpool)
: m_threadPool(threadpool)
{
    if (threadpool == nullptr)
        throw invalid_argument("threadpool");
}

SerialQueueImpl::SerialQueueImpl() : SerialQueueImpl(PlatformThreadPool::Default()) {
    
}

void SerialQueueImpl::VerifyQueue()
{
    if (s_queueStack.get() == nullptr || find(s_queueStack->begin(), s_queueStack->end(), this) == s_queueStack->end())
        throw logic_error("On the wrong queue");
}

IDisposable SerialQueueImpl::DispatchAsync(Action action)
{
    {
        lock_guard<mutex> guard(m_schedulerLock);
        if (m_isDisposed)
            throw logic_error("Cannot call DispatchSync on a disposed queue");
        
        m_asyncActions.push_back(move(action));
        if (m_asyncState == AsyncState::Idle)
        {
            // even though we don't hold m_schedulerLock when asyncActionsAreProcessing is set to false
            // that should be OK as the only "contention" happens up here while we do hold it
            m_asyncState = AsyncState::Scheduled;
            m_threadPool->QueueWorkItem(bind(&SerialQueueImpl::ProcessAsync, this));
        }
    } // unlock
    
    return AnonymousDisposable::CreateShared([&]{
        // we can't "take it out" of the threadpool as not all threadpools support that
        lock_guard<mutex> guard(m_schedulerLock);
        auto iter = find(m_asyncActions.begin(), m_asyncActions.end(), action);
        if(iter != m_asyncActions.end())
            m_asyncActions.erase(iter);
    });
}

void SerialQueueImpl::ProcessAsync()
{
    bool schedulerLockTaken = false;
    
    s_queueStack->push_back(this);
    
    auto finally = create_scope_exit([&]{
        m_asyncState = AsyncState::Idle;
        if (schedulerLockTaken)
            m_schedulerLock.unlock();
        
        s_queueStack->pop_back(); // technically we leak the queue stack threadlocal, but it's probably OK. Windows will free it when the thread exits
    });
    
    m_schedulerLock.lock();
    schedulerLockTaken = true;
    m_asyncState = AsyncState::Processing;
    
    if (m_isDisposed)
        return; // the actions will have been dumped, there's no point doing anything
    
    while (m_asyncActions.size() > 0)
    {
        // get the head of the queue, then release the lock
        auto action = m_asyncActions[0];
        m_asyncActions.erase(m_asyncActions.begin());
        
        m_schedulerLock.unlock();
        schedulerLockTaken = false;
        
        // process the action
        try
        {
            m_executionLock.lock(); // we must lock here or a DispatchSync could run concurrently with the last thing in the queue
            action();
        }
        catch (const exception& exception) // we only catch std::exception here. If the caller throws something else, too bad
        {
            // TODO call unhandled exception filter
            //                var handler = UnhandledException;
            //                if (handler != null)
            //                    handler(this, new UnhandledExceptionEventArgs(exception));
        }
        
        // now re-acquire the lock for the next thing
        assert(!schedulerLockTaken);
        m_schedulerLock.lock();
        schedulerLockTaken = true;
    }
}

void SerialQueueImpl::DispatchSync(Action action)
{
// copy the stack; might be a more optimal way of doing this, it seems to be fast enough
    vector<SerialQueueImpl*> prevStack(s_queueStack->begin(), s_queueStack->end());
    s_queueStack->push_back(this);
    
    bool schedulerLockTaken = false;
    auto finally = create_scope_exit([&]{
        if (schedulerLockTaken)
            m_schedulerLock.unlock();
        
        s_queueStack->pop_back(); // technically we leak the queue stack threadlocal, but it's probably OK. Windows will free it when the thread exits
    });
    
    m_schedulerLock.lock();
    schedulerLockTaken = true;
    
    if (m_isDisposed)
        throw logic_error("Cannot call DispatchSync on a disposed queue");
    
    if(m_asyncState == AsyncState::Idle || find(prevStack.begin(), prevStack.end(), this) != prevStack.end()) // either queue is empty or it's a nested call
    {
        m_schedulerLock.unlock();
        schedulerLockTaken = false;
        
        // process the action
        m_executionLock.lock();
        action(); // DO NOT CATCH EXCEPTIONS. We're excuting synchronously so just let it throw
        return;
    }
    
    // if there is any async stuff scheduled we must also schedule
    // else m_asyncState == AsyncState.Scheduled, OR we fell through from Processing
    condition_variable asyncReady;
    condition_variable syncDone;
    mutex syncMutex;
    
    DispatchAsync([&]{
        asyncReady.notify_one();
        unique_lock<mutex> lock(syncMutex);
        syncDone.wait(lock);
    });
    
    m_schedulerLock.unlock();
    schedulerLockTaken = false;
    
    auto finally2 = create_scope_exit([&]{
        unique_lock<mutex> lock(syncMutex);
        syncDone.notify_one(); // tell the dispatchAsync it can release the lock
    });
    unique_lock<mutex> lock(syncMutex);
    syncDone.wait(lock);
    action(); // DO NOT CATCH EXCEPTIONS. We're excuting synchronously so just let it throw
}


/// <summary>Internal implementation of Dispose</summary>
/// <remarks>We don't have a finalizer (and nor should we) but this method is just following the MS-recommended dispose pattern just in case someone wants to add one in a derived class</remarks>
/// <param name="disposing">true if called via Dispose(), false if called via a Finalizer.</param>
void SerialQueueImpl::Dispose()
{
//    vector<IDisposable> timers;
//    {
        lock_guard<mutex> lock(m_schedulerLock);
        if (m_isDisposed)
            return; // double-dispose
        
        m_isDisposed = true;
        m_asyncActions.clear();
        
//        timers = m_timers;
//        m_timers.clear();
//    }
//    for(auto& t : timers) {
//        t.Dispose();
//    }
}

/// <summary>Schedules the given action to run asynchronously on the queue after dueTime.</summary>
/// <remarks>The function is not guaranteed to run at dueTime as the queue may be busy, it will run when next able.</remarks>
/// <param name="dueTime">Delay before running the action</param>
/// <param name="action">The function to run</param>
/// <returns>A disposable token which you can use to cancel the async action if it has not run yet.
/// It is always safe to dispose this token, even if the async action has already run</returns>
//    virtual IDisposable DispatchAfter(TimeSpan dueTime, Action action)
//    {
//        IDisposable cancel = null;
//        IDisposable timer = null;
//
//        lock (m_schedulerLock)
//        {
//            if (m_isDisposed)
//                throw new ObjectDisposedException("SerialQueue", "Cannot call DispatchAfter on a disposed queue");
//
//            timer = m_threadPool.Schedule(dueTime, () => {
//                lock(m_schedulerLock)
//                {
//                    m_timers.Remove(timer);
//                    if (cancel == null || m_isDisposed) // we've been canceled OR the queue has been disposed
//                        return;
//
//                    // we must call DispatchAsync while still holding m_schedulerLock to prevent a window where we get disposed at this point
//                    cancel = DispatchAsync(action);
//                }
//            });
//            m_timers.Add(timer);
//        }
//
//        cancel = new AnonymousDisposable(() => {
//            lock (m_schedulerLock)
//            m_timers.Remove(timer);
//
//            timer.Dispose();
//        });
//
//        return new AnonymousDisposable(() => {
//            lock (m_schedulerLock) {
//                if (cancel != null) {
//                    cancel.Dispose(); // this will either cancel the timer or cancel the DispatchAsync depending on which stage it's in
//                    cancel = null;
//                }
//            }
//        });
//    }

// ----- SharedDisposable -----

SharedDisposable::SharedDisposable(std::shared_ptr<IDisposable> impl)
: m_impl(move(impl)) { }

void SharedDisposable::Dispose() {
    assert(m_impl != nullptr);
    m_impl->Dispose();
}

AnonymousDisposable::AnonymousDisposable(std::function<void()> action)
: m_action(move(action)) {}

SharedDisposable AnonymousDisposable::CreateShared(std::function<void()> action) {
    shared_ptr<IDisposable> sptr(new AnonymousDisposable(action));
    return sptr;
}

void AnonymousDisposable::Dispose() {
    
}

