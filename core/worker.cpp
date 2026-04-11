#include "worker.h"
#include <thread>
#include <chrono>
#include <cstdio>
#include <array>
using namespace std;
#ifdef _WIN32
    #define popen  _popen
    #define pclose _pclose
#endif

Worker::Worker(string id, Scheduler& scheduler, ResourceManager& resourceManager, function<void(string)> onEventLog)
    : id(id), scheduler(scheduler), resourceManager(resourceManager), status(WorkerStatus::IDLE), currentTaskId(""), currentFile(""), currentUser(""), tasksCompleted(0), running(false), logEvent(onEventLog)
{}

void Worker::start() {
    running = true;
    int workerNum = 0;
    if (!id.empty() && isdigit(id.back()))
        workerNum = id.back() - '1'; // W1=0, W2=1, W3=2, W4=3

    thread([this, workerNum]() {
        this_thread::sleep_for(chrono::milliseconds(workerNum * 150));
        run();
    }).detach();
}

void Worker::stop() { running = false; }

string Worker::executeFile(Task& task) {
    string cmd;
    string outbin = task.filepath + ".out";

    if (task.language == "cpp") {
    #ifdef _WIN32
        cmd = "g++ \"" + task.filepath + "\" -o \"" + outbin + "\" 2>&1 && \"" + outbin + "\" 2>&1";
    #else
        cmd = "g++ \"" + task.filepath + "\" -o \"" + outbin + "\" 2>&1 && timeout 10 \"" + outbin + "\" 2>&1";
    #endif
    }
    else if (task.language == "c") {
    #ifdef _WIN32
        cmd = "gcc \"" + task.filepath + "\" -o \"" + outbin + "\" 2>&1 && \"" + outbin + "\" 2>&1";
    #else
        cmd = "gcc \"" + task.filepath + "\" -o \"" + outbin + "\" 2>&1 && timeout 10 \"" + outbin + "\" 2>&1";
    #endif
    }
    else if (task.language == "python") {
    #ifdef _WIN32
        cmd = "python \"" + task.filepath + "\" 2>&1";
    #else
        cmd = "timeout 10 python3 \"" + task.filepath + "\" 2>&1";
    #endif
    }
    else if (task.language == "java") {
        string className = task.filename.substr(0, task.filename.find_last_of('.'));
        string dir = task.filepath.substr(0, task.filepath.find_last_of('/'));
        cmd = "cd \"" + dir + "\" && javac \"" + task.filepath + "\" 2>&1" + " && timeout 10 java -cp \"" + dir + "\" " + className + " 2>&1";
    }
    else if (task.language == "js") {
        cmd = "timeout 10 node \"" + task.filepath + "\" 2>&1";
    }
    else {
    #ifdef _WIN32
        cmd = "node \"" + task.filepath + "\" 2>&1";
    #else
        cmd = "timeout 10 node \"" + task.filepath + "\" 2>&1";
    #endif
    }

    string output;
    array<char, 256> buffer;
    FILE* pipe = popen(cmd.c_str(), "r");

    if (!pipe) return "Failed to start process.";

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
        output += buffer.data();

    int exitCode = pclose(pipe);
    if (output.empty()) output = "(no output)";
    output += "\n[Exited with code " + to_string(exitCode) + "]";

    return output;
}

void Worker::run() {
    while (running) {

        Task* task = scheduler.getNextTask();

        if (task == nullptr) {
            this_thread::sleep_for(chrono::milliseconds(500));
            continue;
        }

        bool granted = resourceManager.requestResource(task->id, task->resources_needed);

        if (!granted) {
            logEvent(id + " → Task " + task->id + " (" + task->filename + ") denied — unsafe resource state");
            task->status = TaskStat::Denied;
            // Re-queue with short sleep so other workers get a turn first
            this_thread::sleep_for(chrono::milliseconds(200));
            scheduler.addTask(*task);
            delete task;
            this_thread::sleep_for(chrono::milliseconds(300));
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

        // Determine success: exit code 0 means success
        bool failed = (output.find("[Exited with code 0]") == string::npos);

        // Store output so dashboard can display it per task
        {
            lock_guard<mutex> lock(mtx);
            lastOutput    = output;
            lastTaskId    = task->id;
        }

        resourceManager.releaseResource(task->id);

        {
            lock_guard<mutex> lock(mtx);
            status        = WorkerStatus::IDLE;
            currentTaskId = "";
            currentFile   = "";
            currentUser   = "";
            tasksCompleted++;
        }

        if (failed)
            logEvent(id + " → Task " + task->id + " (" + task->filename + ") FAILED — " + summarizeOutput(output));
        else
            logEvent(id + " completed Task " + task->id + " (" + task->filename + ")");

        delete task;

        // Small yield after completing a task so other workers get scheduling turns
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

string Worker::summarizeOutput(const string& output) {
    // Return first error line for log brevity
    size_t pos = output.find('\n');
    string first = (pos != string::npos) ? output.substr(0, pos) : output;
    if (first.size() > 80) first = first.substr(0, 80) + "...";
    return first;
}

WorkerState Worker::getState() const {
    lock_guard<mutex> lock(mtx);
    return {
        id,
        (status == WorkerStatus::BUSY) ? "busy" : "idle",
        currentTaskId,
        currentFile,
        currentUser,
        tasksCompleted,
        lastOutput,
        lastTaskId
    };
}