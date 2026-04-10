#pragma once
#include <string>
#include <mutex>
#include <functional>
#include "task.h"
#include "scheduler.h"
#include "resource_manager.h"
using namespace std;

enum class WorkerStatus { IDLE, BUSY };

struct WorkerState {
    string id;
    string status;
    string current_task;      // task ID
    string current_file;      // filename being executed
    string current_user;      // submitted_by
    int    tasks_completed;
};

class Worker {
public:
    Worker(string id, Scheduler& scheduler, ResourceManager& resourceManager, function<void(string)> onEventLog);

    void start();
    void stop();
    WorkerState getState() const;

private:
    string           id;
    Scheduler&       scheduler;
    ResourceManager& resourceManager;
    WorkerStatus     status;
    string           currentTaskId;
    string           currentFile;
    string           currentUser;
    int              tasksCompleted;
    bool             running;
    mutable mutex    mtx;
    function<void(string)> logEvent;

    void run();

    string executeFile(Task& task);
};