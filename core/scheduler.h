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

    // Add a new task to the queue
    void addTask(const Task& task);

    // Get next task based on active mode
    // returns nullptr if queue is empty
    Task* getNextTask();

    // Read scheduler_mode.txt and update active mode
    void refreshMode();

    // returns current mode as string
    string getModeString() const;

    // returns current queue snapshot — used by main.cpp for state.json
    vector<Task> getQueue() const;

    // Remove a specific task from queue (used by deadlock recovery)
    void removeTask(const string& taskId);

    // Returns queue size
    int size() const;

private:
    deque<Task>  taskQueue;
    SchedulerMode mode;
    mutable mutex mtx;

    // Path to mode flag file
    const string MODE_FILE = "../../shared/scheduler_mode.txt";
};