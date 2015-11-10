//
//  Interfaces.hpp
//  SerialQueueCPP
//
//  Created by Orion Edwards on 2/11/15.
//  Copyright © 2015 Orion Edwards. All rights reserved.
//

#ifndef Interfaces_h
#define Interfaces_h

#include <functional>
#include <memory>

struct IDisposable {
    virtual void Dispose() = 0;
};

// refcounting wrapper (using shared ptr) over another kind of disposable
class SharedDisposable : public IDisposable {
    template<typename TDisposable>
    struct SharedDisposableImpl : public IDisposable {
        TDisposable InnerDisposable;
        void Dispose() override {
            InnerDisposable.Dispose();
        }
    };
    
    const std::shared_ptr<IDisposable> m_impl;
    SharedDisposable(std::shared_ptr<IDisposable> impl);
    
    friend class AnonymousDisposable;
public:
    void Dispose() override;
};

class AnonymousDisposable : public IDisposable {
    std::function<void()> m_action;
    
    AnonymousDisposable(std::function<void()> action);
    
    AnonymousDisposable(const AnonymousDisposable& other) = delete;
    AnonymousDisposable& operator=(const AnonymousDisposable& other) = delete;
    
public:
    // implicit new/move constructor are used
    
    static SharedDisposable CreateShared(std::function<void()> action);
    void Dispose() override;
};

struct IDispatchQueue {
    virtual IDisposable DispatchAsync(std::function<void()> action) = 0;
    
    virtual void DispatchSync(std::function<void()> action) = 0;
    
    //    virtual IDisposable DispatchAfter(std::chrono::milliseconds delay, std::function<void()> action) = 0;
    
    virtual void VerifyQueue() = 0;
};

struct IThreadPool {
    virtual void QueueWorkItem(std::function<void()> action) = 0;
    
    //    virtual IDisposable Schedule(std::chrono::milliseconds delay, std::function<void()> action) = 0 ;
};

struct PlatformThreadPool : public IThreadPool {
    static std::shared_ptr<IThreadPool> Default();
};

#endif /* Interfaces_h */
