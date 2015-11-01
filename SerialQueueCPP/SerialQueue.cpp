//
//  SerialQueue.cpp
//  SerialQueueCPP
//
//  Created by Orion Edwards on 1/11/15.
//  Copyright © 2015 Orion Edwards. All rights reserved.
//

#include "SerialQueue.hpp"
using namespace std;

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
    if (s_queueStack == nullptr || !s_queueStack.Contains(this))
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
            m_threadPool->QueueWorkItem(ProcessAsync);
        }
    } // unlock
    
    return AnonymousDisposable::Create([&]{
        // we can't "take it out" of the threadpool as not all threadpools support that
        lock_guard<mutex> guard(m_schedulerLock);
        m_asyncActions.remove(action);
    });
}

void SerialQueueImpl::ProcessAsync()
{
    bool schedulerLockTaken = false;
    if (s_queueStack == null)
        s_queueStack = new Stack<IDispatchQueue>();
        s_queueStack.Push(this);
        try
    {
        Monitor.Enter(m_schedulerLock, ref schedulerLockTaken);
        Debug.Assert(schedulerLockTaken);
        
        m_asyncState = AsyncState.Processing;
        
        if (m_isDisposed)
            return; // the actions will have been dumped, there's no point doing anything
        
        while (m_asyncActions.Count > 0)
        {
            // get the head of the queue, then release the lock
            var action = m_asyncActions[0];
            m_asyncActions.RemoveAt(0);
            Monitor.Exit(m_schedulerLock);
            schedulerLockTaken = false;
            
            // process the action
            try
            {
                lock(m_executionLock) // we must lock here or a DispatchSync could run concurrently with the last thing in the queue
                action();
            }
            catch (Exception exception)
            {
                var handler = UnhandledException;
                if (handler != null)
                    handler(this, new UnhandledExceptionEventArgs(exception));
                    }
            
            // now re-acquire the lock for the next thing
            Debug.Assert(!schedulerLockTaken);
            Monitor.Enter(m_schedulerLock, ref schedulerLockTaken);
            Debug.Assert(schedulerLockTaken);
        }
    }
    finally
    {
        m_asyncState = AsyncState.Idle;
        if (schedulerLockTaken)
            Monitor.Exit(m_schedulerLock);
            
            s_queueStack.Pop(); // technically we leak the queue stack threadlocal, but it's probably OK. Windows will free it when the thread exits
            }
}

void SerialQueueImpl::DispatchSync(Action action);
{
    if (s_queueStack == null)
        s_queueStack = new Stack<IDispatchQueue>();
        var prevStack = s_queueStack.ToArray(); // there might be a more optimal way of doing this, it seems to be fast enough
        s_queueStack.Push(this);
        
        bool schedulerLockTaken = false;
        try
    {
        Monitor.Enter(m_schedulerLock, ref schedulerLockTaken);
        Debug.Assert(schedulerLockTaken);
        
        if (m_isDisposed)
            throw new ObjectDisposedException("SerialQueue", "Cannot call DispatchSync on a disposed queue");
            
            if(m_asyncState == AsyncState.Idle || prevStack.Contains(this)) // either queue is empty or it's a nested call
            {
                Monitor.Exit(m_schedulerLock);
                schedulerLockTaken = false;
                
                // process the action
                lock (m_executionLock)
                action(); // DO NOT CATCH EXCEPTIONS. We're excuting synchronously so just let it throw
                return;
            }
        
        // if there is any async stuff scheduled we must also schedule
        // else m_asyncState == AsyncState.Scheduled, OR we fell through from Processing
        var asyncReady = new ManualResetEvent(false);
        var syncDone = new ManualResetEvent(false);
        DispatchAsync(() => {
            asyncReady.Set();
            syncDone.WaitOne();
        });
        Monitor.Exit(m_schedulerLock);
        schedulerLockTaken = false;
        
        try
        {
            asyncReady.WaitOne();
            action(); // DO NOT CATCH EXCEPTIONS. We're excuting synchronously so just let it throw
        }
        finally
        {
            syncDone.Set(); // tell the dispatchAsync it can release the lock
        }
    }
    finally
    {
        if (schedulerLockTaken)
            Monitor.Exit(m_schedulerLock);
            
            s_queueStack.Pop(); // technically we leak the queue stack threadlocal, but it's probably OK. Windows will free it when the thread exits
            }
}


/// <summary>Internal implementation of Dispose</summary>
/// <remarks>We don't have a finalizer (and nor should we) but this method is just following the MS-recommended dispose pattern just in case someone wants to add one in a derived class</remarks>
/// <param name="disposing">true if called via Dispose(), false if called via a Finalizer.</param>
void SerialQueueImpl::Dispose()
{
    IDisposable[] timers;
    lock (m_schedulerLock)
    {
        if (m_isDisposed)
            return; // double-dispose
        
        m_isDisposed = true;
        m_asyncActions.Clear();
        
        timers = m_timers.ToArray();
        m_timers.Clear();
    }
    foreach (var t in timers)
    t.Dispose();
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
