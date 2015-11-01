//
//  Disposable.cpp
//  SerialQueueCPP
//
//  Created by Orion Edwards on 2/11/15.
//  Copyright © 2015 Orion Edwards. All rights reserved.
//

#include "interfaces.hpp"
#include <cassert>
using namespace std;


void SharedDisposable::Dispose() {
    assert(m_impl != nullptr);
    m_impl->Dispose();
}

AnonymousDisposable::AnonymousDisposable(std::function<void()> action)
: m_action(move(action)) {}

SharedDisposable AnonymousDisposable::CreateShared(std::function<void()> action) {
    shared_ptr<IDisposable> sptr = make_shared<AnonymousDisposable>(action);
}

void AnonymousDisposable::Dispose() {
    
}
