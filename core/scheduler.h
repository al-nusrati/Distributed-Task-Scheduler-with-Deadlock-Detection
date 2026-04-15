#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include "task.h"

using namespace std;

enum class SchedulerMode {
    PRIORITY,
    ROUND_ROBIN
};

class Scheduler {
public:
    Scheduler();

    // Add a new task to the appropriate queue based on its scheduling_mode
    void addTask(const Task& task);

    // Get next task: first try priority queue (highest priority), else RR queue (FIFO)
    // Returns nullptr if both queues are empty
    Task* getNextTask();

    // Read scheduler_mode.txt (global override) – still used for dashboard display
    void refreshMode();

    // returns current global mode as string (for state.json)
    string getModeString() const;

    // returns snapshot of both queues combined (for state.json)
    vector<Task> getQueue() const;

    // Remove a specific task from whichever queue it resides in
    void removeTask(const string& taskId);

    // Returns total number of tasks in both queues
    int size() const;

private:
    deque<Task>  priorityQueue;   // tasks with scheduling_mode == "priority"
    deque<Task>  rrQueue;         // tasks with scheduling_mode == "round_robin"
    SchedulerMode globalMode;     // kept for display only, does not affect task selection
    mutable mutex mtx;

    const string MODE_FILE = "../../shared/scheduler_mode.txt";
};