#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "resource_manager.h"

using namespace std;

struct DeadlockState {
    string         status;          // "none", "detected", "resolved"
    string         detected_at;     // timestamp
    vector<string> involved_tasks;  // tasks in the cycle
    string         action_taken;    // what was killed
};

class DeadlockDetector {
public:
    DeadlockDetector(ResourceManager& rm, function<void(DeadlockState)> onDeadlock);
    void start();
    void stop();
    DeadlockState getCurrentState() const;

private:
    ResourceManager&              resourceManager;
    function<void(DeadlockState)> onDeadlockCallback;
    DeadlockState                 currentState;
    bool                          running;

    vector<string> detectCycle();
    string recoverFromDeadlock(const vector<string>& cycle);
    string getCurrentTime() const;
};