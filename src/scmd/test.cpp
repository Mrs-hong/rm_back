
#include "common/config.h"
#include "service_manger/service_manager.h"

#include <iostream>
void TestInstall();
void TestStartService();
void TestStopService();
void DoTest() {
}

void TestInstall() {
    std::cout << "TestInstall" << std::endl;
}
void TestStartService() {
    std::cout << "TestStartService" << std::endl;
}
void TestStopService() {
    std::cout << "TestStopService" << std::endl;
}
