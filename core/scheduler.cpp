#include "scheduler.h"
#include <fstream>
#include <algorithm>

using namespace std;

Scheduler::Scheduler() : mode(SchedulerMode::PRIORITY) {}

void Scheduler::addTask(const Task& task) {
    lock_guard<mutex> lock(mtx);
    taskQueue.push_back(task);
}

Task* Scheduler::getNextTask() {
    lock_guard<mutex> lock(mtx);
    refreshMode(); 

    if (taskQueue.empty()) return nullptr;

    if (mode == SchedulerMode::PRIORITY) {
        // Find highest priority task
        auto highest = max_element(
            taskQueue.begin(), taskQueue.end(),
            [](const Task& a, const Task& b) {
                return a.priority < b.priority;
            }
        );

        Task* picked = new Task(*highest);
        taskQueue.erase(highest);
        return picked;

    } else {
        // RR — just take front
        Task* picked = new Task(taskQueue.front());
        taskQueue.pop_front();
        return picked;
    }
}


void Scheduler::refreshMode() {
    ifstream file(MODE_FILE);
    if (!file.is_open()) return;

    string modeStr;
    file >> modeStr;

    if (modeStr == "round_robin")
        mode = SchedulerMode::ROUND_ROBIN;
    else
        mode = SchedulerMode::PRIORITY;
}


string Scheduler::getModeString() const {
    return (mode == SchedulerMode::PRIORITY) ? "priority" : "round_robin";
}

vector<Task> Scheduler::getQueue() const {
    lock_guard<mutex> lock(mtx);
    return vector<Task>(taskQueue.begin(), taskQueue.end());
}

void Scheduler::removeTask(const string& taskId) {
    lock_guard<mutex> lock(mtx);
    taskQueue.erase(
        remove_if(taskQueue.begin(), taskQueue.end(),
            [&taskId](const Task& t) { return t.id == taskId; }),
        taskQueue.end()
    );
}

int Scheduler::size() const {
    lock_guard<mutex> lock(mtx);
    return taskQueue.size();
}