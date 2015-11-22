//
//  SerialQueue.Test.cpp
//  SerialQueueCPP
//
//  Created by Orion Edwards on 23/11/15.
//  Copyright © 2015 Orion Edwards. All rights reserved.
//

#include "SerialQueue.hpp"
#include "gtest/gtest.h"

using namespace std;

struct InvalidThreadPool : public IThreadPool {
    void QueueWorkItem(std::function<void()> action) override {
        throw std::logic_error("not implemented");
    }
};

// ----- DispatchSync tests -----

TEST(DispatchSync, RunsNormally) {
    SerialQueue q(make_shared<InvalidThreadPool>());
    vector<int> hit;
    q.DispatchSync([&]{
        hit.push_back(1);
    });
    EXPECT_EQ(1, hit.size());
}

//[TestMethod]
//public void DispatchSyncRunsNormally()
//{
//    var q = new SerialQueue(new InvalidThreadPool());
//    var hit = new List<int>();
//    q.DispatchSync(() => {
//        hit.Add(1);
//    });
//    CollectionAssert.AreEqual(new[] { 1 }, hit);
//}
//
//[TestMethod]
//public void NestedDispatchSyncDoesntDeadlock()
//{
//    var q = new SerialQueue(new InvalidThreadPool());
//    var hit = new List<int>();
//    q.DispatchSync(() => {
//        hit.Add(1);
//        q.DispatchSync(() => {
//            hit.Add(2);
//        });
//        hit.Add(3);
//    });
//    CollectionAssert.AreEqual(new[] { 1, 2, 3 }, hit);
//}
//
//[TestMethod]
//public void NestedDispatchSyncInsideDispatchAsyncDoesntDeadlock()
//{
//    var q = new SerialQueue(new TaskThreadPool());
//    var hit = new List<int>();
//    var mre = new ManualResetEvent(false);
//    q.DispatchAsync(() => {
//        hit.Add(1);
//        q.DispatchSync(() => {
//            hit.Add(2);
//        });
//        hit.Add(3);
//        mre.Set();
//    });
//    mre.WaitOne();
//    CollectionAssert.AreEqual(new[] { 1, 2, 3 }, hit);
//}
//
//[TestMethod]
//public void DispatchSyncConcurrentWithAnotherDispatchAsyncWorksProperly()
//{
//    var q = new SerialQueue(new TaskThreadPool());
//    var hit = new List<int>();
//    var are = new AutoResetEvent(false);
//    q.DispatchAsync(() => {
//        hit.Add(1);
//        are.Set();
//        Thread.Sleep(100); // we can't block on an event as that would deadlock, we just have to slow it down enough to force the DispatchSync to wait
//        hit.Add(2);
//    });
//    are.WaitOne();
//    q.DispatchSync(() => {
//        hit.Add(3);
//    });
//    
//    CollectionAssert.AreEqual(new[] { 1, 2, 3 }, hit);
//}