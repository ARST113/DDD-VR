#pragma once
#include <string>

class OpenXrSession {
public:
    bool initialize();
    bool begin();
    void pollEvents();
    bool runtimeAvailable() const { return runtimeAvailable_; }
    const std::string& lastError() const { return lastError_; }
private:
    bool runtimeAvailable_ = false;
    std::string lastError_;
};
