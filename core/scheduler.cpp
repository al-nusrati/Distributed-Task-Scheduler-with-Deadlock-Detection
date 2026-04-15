#include "scheduler.h"
#include <fstream>
#include <algorithm>

using namespace std;

Scheduler::Scheduler() : globalMode(SchedulerMode::PRIORITY) {}

void Scheduler::addTask(const Task& task) {
    lock_guard<mutex> lock(mtx);
    if (task.scheduling_mode == "round_robin")
        rrQueue.push_back(task);
    else
        priorityQueue.push_back(task);
}

Task* Scheduler::getNextTask() {
    lock_guard<mutex> lock(mtx);
    refreshMode();  // still read global mode for display purposes only

    // Always try priority queue first (higher importance)
    if (!priorityQueue.empty()) {
        // Find highest priority task
        auto highest = max_element(
            priorityQueue.begin(), priorityQueue.end(),
            [](const Task& a, const Task& b) {
                return a.priority < b.priority;
            }
        );
        Task* picked = new Task(*highest);
        priorityQueue.erase(highest);
        return picked;
    }

    // Otherwise take from RR queue (FIFO)
    if (!rrQueue.empty()) {
        Task* picked = new Task(rrQueue.front());
        rrQueue.pop_front();
        return picked;
    }

    return nullptr;
}

void Scheduler::refreshMode() {
    ifstream file(MODE_FILE);
    if (!file.is_open()) return;

    string modeStr;
    file >> modeStr;

    if (modeStr == "round_robin")
        globalMode = SchedulerMode::ROUND_ROBIN;
    else
        globalMode = SchedulerMode::PRIORITY;
}

string Scheduler::getModeString() const {
    return (globalMode == SchedulerMode::PRIORITY) ? "priority" : "round_robin";
}

vector<Task> Scheduler::getQueue() const {
    lock_guard<mutex> lock(mtx);
    vector<Task> all;
    all.insert(all.end(), priorityQueue.begin(), priorityQueue.end());
    all.insert(all.end(), rrQueue.begin(), rrQueue.end());
    return all;
}

void Scheduler::removeTask(const string& taskId) {
    lock_guard<mutex> lock(mtx);
    auto removeFrom = [&](deque<Task>& q) {
        q.erase(remove_if(q.begin(), q.end(),
                [&taskId](const Task& t) { return t.id == taskId; }),
                q.end());
    };
    removeFrom(priorityQueue);
    removeFrom(rrQueue);
}

int Scheduler::size() const {
    lock_guard<mutex> lock(mtx);
    return priorityQueue.size() + rrQueue.size();
}