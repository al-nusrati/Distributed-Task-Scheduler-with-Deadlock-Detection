#include "worker.h"
#include <thread>
#include <chrono>
#include <cstdio>
#include <array>
#include <stdexcept>
using namespace std;
#define popen  _popen
#define pclose _pclose

Worker::Worker(string id, Scheduler& scheduler, ResourceManager& resourceManager, function<void(string)> onEventLog)
    : id(id), scheduler(scheduler),resourceManager(resourceManager), status(WorkerStatus::IDLE), currentTaskId(""), currentFile(""),currentUser(""), tasksCompleted(0), running(false), logEvent(onEventLog)
{}

void Worker::start() {
    running = true;
    thread(&Worker::run, this).detach();
}

void Worker::stop() { running = false; }

string Worker::executeFile(Task& task) {
    string cmd;

    if (task.language == "cpp") {
        string outbin = task.filepath + ".out";
        cmd = "g++ " + task.filepath + " -o " + outbin + " 2>&1 && timeout 10 " + outbin + " 2>&1";
    }
    else if (task.language == "c") {
        string outbin = task.filepath + ".out";
        cmd = "gcc " + task.filepath + " -o " + outbin + " 2>&1 && timeout 10 " + outbin + " 2>&1";
    }
    else if (task.language == "python") {
        cmd = "timeout 10 python3 " + task.filepath + " 2>&1";
    }
    else if (task.language == "js") {
        cmd = "timeout 10 node " + task.filepath + " 2>&1";
    }
    else {
        return "Unsupported language: " + task.language;
    }

    string output;
    array<char, 256> buffer;
    FILE* pipe = popen(cmd.c_str(), "r");

    if (!pipe) return "Failed to execute process.";

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
        output += buffer.data();

    int exitCode = pclose(pipe);
    output += "\n[Exited with code " + to_string(exitCode) + "]";

    return output;
}

void Worker::run() {
    while (running) {

        Task* task = scheduler.getNextTask();

        if (task == nullptr) {
            this_thread::sleep_for(chrono::seconds(1));
            continue;
        }

        bool granted = resourceManager.requestResource(task->id, task->resources_needed);

        if (!granted) {
            task->status = TaskStat::Denied;
            logEvent(id + " → Task " + task->id + " (" + task->filename + ") denied — unsafe state");
            task->status = TaskStat::Waiting;
            scheduler.addTask(*task);
            delete task;
            this_thread::sleep_for(chrono::seconds(1));
            continue;
        }

        {
            lock_guard<mutex> lock(mtx);
            status        = WorkerStatus::BUSY;
            currentTaskId = task->id;
            currentFile   = task->filename;
            currentUser   = task->submitted_by;
            task->status  = TaskStat::Processing;
        }

        logEvent(id + " picked up Task " + task->id + " (" + task->filename + ") from " + task->submitted_by);

        string output = executeFile(*task);

        bool failed = (output.find("error") != string::npos && output.find("[Exited with code 0]") == string::npos);

        resourceManager.releaseResource(task->id);

        {
            lock_guard<mutex> lock(mtx);
            status        = WorkerStatus::IDLE;
            currentTaskId = "";
            currentFile   = "";
            currentUser   = "";
            tasksCompleted++;
        }

        if (failed) {
            logEvent(id + " → Task " + task->id + " (" + task->filename + ") FAILED");
        } else {
            logEvent(id + " completed Task " + task->id + " (" + task->filename + ")");
        }

        delete task;
    }
}

WorkerState Worker::getState() const {
    lock_guard<mutex> lock(mtx);
    return {
        id,
        (status == WorkerStatus::BUSY) ? "busy" : "idle",
        currentTaskId,
        currentFile,
        currentUser,
        tasksCompleted
    };
}