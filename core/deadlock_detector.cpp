#include "deadlock_detector.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

using namespace std;

DeadlockDetector::DeadlockDetector(ResourceManager&  rm, function<void(DeadlockState)> onDeadlock)
    : resourceManager(rm), onDeadlockCallback(onDeadlock), running(false)
{   currentState = { "none", "", {}, "" };}
void DeadlockDetector::start() {
    running = true;

    thread([this]() {
        while (running) {
            this_thread::sleep_for(chrono::seconds(3));

            vector<string> cycle = detectCycle();

            if (!cycle.empty()) {
                // Deadlock detected
                currentState.status         = "detected";
                currentState.detected_at    = getCurrentTime();
                currentState.involved_tasks = cycle;
                currentState.action_taken   = "";

                onDeadlockCallback(currentState);

                // Recover
                string killed = recoverFromDeadlock(cycle);

                // Update state to resolved
                currentState.status       = "resolved";
                currentState.action_taken = killed + " terminated — resources freed";

                onDeadlockCallback(currentState);

            } else {
                // No deadlock — reset state
                currentState = { "none", "", {}, "" };
            }
        }
    }).detach(); // runs independently
}
void DeadlockDetector::stop() {
    running = false;
}

DeadlockState DeadlockDetector::getCurrentState() const {
    return currentState;
}
vector<string> DeadlockDetector::detectCycle() {
    auto allocation = resourceManager.getAllocationMatrix();
    auto allResources = resourceManager.getAllResources();
    map<string, vector<string>> waitFor;

    for (const auto& [waitingTask, _] : allocation) {
        for (const Resource& r : allResources) {
            if (r.available == 0) {
                bool alreadyHolding = false;
                for (const string& holder : r.held_by)
                    if (holder == waitingTask) { alreadyHolding = true; break; }

                if (!alreadyHolding) {
                    for (const string& holder : r.held_by) {
                        if (holder != waitingTask)
                            waitFor[waitingTask].push_back(holder);
                    }
                }
            }
        }
    }

    // DFS cycle detection
    map<string, int> visited; // 0=unvisited, 1=in stack, 2=done
    vector<string>   cycle;

    function<bool(const string&)> dfs = [&](const string& node) -> bool {
        visited[node] = 1;
        cycle.push_back(node);

        for (const string& neighbor : waitFor[node]) {
            if (visited[neighbor] == 1) {
                // Found cycle — trim to just the cycle
                auto it = find(cycle.begin(), cycle.end(), neighbor);
                cycle = vector<string>(it, cycle.end());
                return true;
            }
            if (visited[neighbor] == 0 && dfs(neighbor))
                return true;
        }

        cycle.pop_back();
        visited[node] = 2;
        return false;
    };

    for (const auto& [taskId, _] : waitFor) {
        if (visited[taskId] == 0) {
            if (dfs(taskId))
                return cycle;
        }
    }

    return {}; // no cycle found
}

string DeadlockDetector::recoverFromDeadlock(const vector<string>& cycle) {
    string victim = cycle.back();
    resourceManager.releaseResource(victim);

    return victim;
}

string DeadlockDetector::getCurrentTime() const {
    time_t now = time(nullptr);
    tm* t = localtime(&now);
    ostringstream oss;
    oss << setfill('0')
        << setw(2) << t->tm_hour << ":"
        << setw(2) << t->tm_min  << ":"
        << setw(2) << t->tm_sec;
    return oss.str();
}