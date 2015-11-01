//
//  PlatformThreadPool.cpp
//  SerialQueueCPP
//
//  Created by Orion Edwards on 2/11/15.
//  Copyright © 2015 Orion Edwards. All rights reserved.
//

#include "Interfaces.hpp"

/*static*/ std::shared_ptr<IThreadPool> PlatformThreadPool::Default() {
    return {};
}