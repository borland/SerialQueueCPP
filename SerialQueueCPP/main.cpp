//
//  main.cpp
//  SerialQueueCPP
//
//  Created by Orion Edwards on 1/11/15.
//  Copyright © 2015 Orion Edwards. All rights reserved.
//

#include <iostream>
#include "SerialQueue.hpp"

#include "gtest/gtest.h"

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}