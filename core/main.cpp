#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <mutex>
#include <deque>
#include <map>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <filesystem>           // C++17 cross-platform path / directory ops

// File-locking headers (Unix / Linux / Docker only)
#ifndef _WIN32
#  include <sys/file.h>         // flock()
#  include <fcntl.h>            // open(), O_CREAT, O_RDWR
#  include <unistd.h>           // close()
#endif

#include "task.h"
#include "scheduler.h"
#include "resource_manager.h"
#include "banker.h"
#include "deadlock_detector.h"
#include "worker.h"
#include "nlohmann/json.hpp"

using namespace std;
namespace fs = std::filesystem;
using json = nlohmann::json;

const string STATE_FILE  = "../../shared/state.json";
const string INPUT_FILE  = "../../shared/input.json";
const string LOCK_FILE   = "../../shared/input.lock";
const string MODE_FILE   = "../../shared/scheduler_mode.txt";
const string UPLOADS_DIR = "../../shared/uploads/";

deque<json> eventLog;
mutex       eventLogMtx;
int         taskCounter = 0;
mutex       taskCounterMtx;

map<string, string> taskOutputMap;
mutex               taskOutputMtx;

DeadlockState currentDeadlockState = { "none", "", {}, "" };
mutex         deadlockMtx;

vector<string> assignResources(const string& language) {
    if (language == "cpp" || language == "c")  return { "R1", "R2" };
    else if (language == "python")             return { "R2", "R3" };
    else if (language == "java")               return { "R1", "R2", "R3" };
    else if (language == "js")                 return { "R3", "R4" };
    return { "R1" };
}

int assignPriority(const string& filepath) {
    ifstream f(filepath, ios::binary | ios::ate);
    if (!f.is_open()) return 5;
    long size = (long)f.tellg();
    f.close();
    if (size < 1024)       return 9;
    else if (size < 5120)  return 7;
    else if (size < 20480) return 5;
    else if (size < 51200) return 3;
    else                   return 1;
}

string getTimestamp() {
    time_t now = time(nullptr);
    tm* t = localtime(&now);
    ostringstream oss;
    oss << setfill('0')
        << setw(2) << t->tm_hour << ":"
        << setw(2) << t->tm_min  << ":"
        << setw(2) << t->tm_sec;
    return oss.str();
}

string getLogType(const string& msg) {
    if (msg.find("completed") != string::npos) return "task_completed";
    if (msg.find("picked up") != string::npos) return "task_started";
    if (msg.find("rejected")  != string::npos) return "task_rejected";
    if (msg.find("denied")    != string::npos) return "task_denied";
    if (msg.find("FAILED")    != string::npos) return "task_failed";
    if (msg.find("Deadlock")  != string::npos) return "deadlock_detected";
    if (msg.find("resolved")  != string::npos) return "deadlock_resolved";
    if (msg.find("added")     != string::npos) return "task_added";
    return "info";
}

void logEvent(const string& message) {
    lock_guard<mutex> lock(eventLogMtx);
    json entry;
    entry["time"]    = getTimestamp();
    entry["type"]    = getLogType(message);
    entry["message"] = message;
    eventLog.push_front(entry);
    if (eventLog.size() > 50) eventLog.pop_back();
}

void readInputFile(Scheduler& scheduler) {
    const vector<string> ALLOWED = { "cpp", "c", "python", "java", "js" };
    bool wasProcessingEnabled = false;

    fs::create_directories(UPLOADS_DIR);

#ifndef _WIN32
    {
        int initFd = open(LOCK_FILE.c_str(), O_CREAT | O_RDWR, 0644);
        if (initFd >= 0) close(initFd);
    }
#endif

    while (true) {
        this_thread::sleep_for(chrono::seconds(1));

#ifndef _WIN32
        int lockFd = open(LOCK_FILE.c_str(), O_CREAT | O_RDWR, 0644);
        if (lockFd < 0) continue;
        flock(lockFd, LOCK_EX);
#endif

        json data;
        {
            ifstream file(INPUT_FILE);
            if (!file.is_open()) {
#ifndef _WIN32
                flock(lockFd, LOCK_UN); close(lockFd);
#endif
                continue;
            }
            try { file >> data; }
            catch (...) {
#ifndef _WIN32
                flock(lockFd, LOCK_UN); close(lockFd);
#endif
                continue;
            }
        }

        bool processingEnabled = data.value("processing_enabled", false);
        if (!processingEnabled) {
            wasProcessingEnabled = false;
#ifndef _WIN32
            flock(lockFd, LOCK_UN); close(lockFd);
#endif
            continue;
        }

        if (!data.contains("pending_tasks") || data["pending_tasks"].empty()) {
            data["processing_enabled"] = false;
            { ofstream out(INPUT_FILE); out << data.dump(2); }
#ifndef _WIN32
            flock(lockFd, LOCK_UN); close(lockFd);
#endif
            if (wasProcessingEnabled)
                logEvent("Input queue drained - scheduler paused until Start is pressed again");
            wasProcessingEnabled = false;
            continue;
        }

        wasProcessingEnabled = true;

        json pendingTasks          = data["pending_tasks"];
        data["pending_tasks"]      = json::array();
        data["processing_enabled"] = false;
        { ofstream out(INPUT_FILE); out << data.dump(2); }

#ifndef _WIN32
        flock(lockFd, LOCK_UN); close(lockFd);
#endif

        logEvent("Input queue drained - scheduler paused until Start is pressed again");
        wasProcessingEnabled = false;

        for (auto& t : pendingTasks) {
            string lang  = t.value("language",     "");
            string fname = t.value("filename",     "unknown");
            string user  = t.value("submitted_by", "Anonymous");
            string schedMode = t.value("scheduling_mode", "priority");
            if (schedMode != "priority" && schedMode != "round_robin")
                schedMode = "priority";

            if (find(ALLOWED.begin(), ALLOWED.end(), lang) == ALLOWED.end()) {
                logEvent("Task rejected — unsupported file type: " + fname
                         + " by " + user + " (allowed: cpp, c, python, java, js)");
                continue;
            }

            int id;
            {
                lock_guard<mutex> lk(taskCounterMtx);
                id = ++taskCounter;
            }
            string taskId  = "T" + to_string(id);
            string content = t.value("file_content", "");

            string fpath = UPLOADS_DIR + taskId + "_" + fname;
            { ofstream fout(fpath); fout << content; }

            int priority             = assignPriority(fpath);
            vector<string> resources = assignResources(lang);

            Task newTask(taskId, priority, fname, fpath, lang, user, resources, schedMode);
            scheduler.addTask(newTask);

            logEvent("Task " + taskId + " added — " + fname
                     + " by " + user + " (priority " + to_string(priority)
                     + ", mode " + schedMode + ")");
        }
    }
}

void writeStateFile(
    Scheduler&       scheduler,
    ResourceManager& resourceManager,
    vector<Worker*>& workers,
    int&             uptimeSeconds)
{
    while (true) {
        this_thread::sleep_for(chrono::seconds(1));
        uptimeSeconds++;

        json state;

        int totalCompleted = 0;
        for (Worker* w : workers)
            totalCompleted += w->getState().tasks_completed;

        state["system"]["status"]                = "running";
        state["system"]["active_scheduler"]      = scheduler.getModeString();
        state["system"]["uptime_seconds"]        = uptimeSeconds;
        state["system"]["total_tasks_completed"] = totalCompleted;

        state["workers"] = json::array();
        for (Worker* w : workers) {
            WorkerState ws = w->getState();
            json worker;
            worker["id"]              = ws.id;
            worker["status"]          = ws.status;
            worker["current_task"]    = ws.current_task.empty()  ? json(nullptr) : json(ws.current_task);
            worker["current_file"]    = ws.current_file.empty()  ? json(nullptr) : json(ws.current_file);
            worker["current_user"]    = ws.current_user.empty()  ? json(nullptr) : json(ws.current_user);
            worker["tasks_completed"] = ws.tasks_completed;
            worker["last_output"]     = ws.last_output.empty()   ? json(nullptr) : json(ws.last_output);
            worker["last_task_id"]    = ws.last_task_id.empty()  ? json(nullptr) : json(ws.last_task_id);
            state["workers"].push_back(worker);
        }

        state["task_queue"] = json::array();
        for (const Task& t : scheduler.getQueue()) {
            json task;
            task["id"]              = t.id;
            task["priority"]        = t.priority;
            task["filename"]        = t.filename;
            task["language"]        = t.language;
            task["submitted_by"]    = t.submitted_by;
            task["status"]          = t.statusToString();
            task["scheduling_mode"] = t.scheduling_mode;
            state["task_queue"].push_back(task);
        }

        state["resources"] = json::array();
        for (const Resource& r : resourceManager.getAllResources()) {
            json res;
            res["id"]        = r.id;
            res["name"]      = r.name;
            res["total"]     = r.total;
            res["available"] = r.available;
            res["held_by"]   = r.held_by;
            state["resources"].push_back(res);
        }

        {
            lock_guard<mutex> lock(deadlockMtx);
            state["deadlock"]["status"]         = currentDeadlockState.status;
            state["deadlock"]["detected_at"]    = currentDeadlockState.detected_at.empty()
                                                    ? json(nullptr) : json(currentDeadlockState.detected_at);
            state["deadlock"]["involved_tasks"] = currentDeadlockState.involved_tasks;
            state["deadlock"]["action_taken"]   = currentDeadlockState.action_taken.empty()
                                                    ? json(nullptr) : json(currentDeadlockState.action_taken);
        }

        {
            lock_guard<mutex> lock(eventLogMtx);
            state["event_log"] = json::array();
            for (const json& entry : eventLog)
                state["event_log"].push_back(entry);
        }

        {
            lock_guard<mutex> lk(taskOutputMtx);
            state["task_outputs"] = json::object();
            for (const auto& [tid, out] : taskOutputMap)
                state["task_outputs"][tid] = out;
        }

        ofstream out(STATE_FILE);
        out << state.dump(2);
    }
}

void collectOutputs(vector<Worker*>& workers) {
    while (true) {
        this_thread::sleep_for(chrono::milliseconds(500));
        for (Worker* w : workers) {
            WorkerState ws = w->getState();
            if (!ws.last_task_id.empty() && !ws.last_output.empty()) {
                lock_guard<mutex> lk(taskOutputMtx);
                taskOutputMap[ws.last_task_id] = ws.last_output;
            }
        }
    }
}

int main() {
    cout << "ParagonEngine — Starting..." << endl;

    Scheduler       scheduler;
    ResourceManager resourceManager;

    vector<Worker*> workers;
    for (int i = 1; i <= 4; i++) {
        Worker* w = new Worker("W" + to_string(i), scheduler, resourceManager, logEvent);
        workers.push_back(w);
    }

    DeadlockDetector detector(resourceManager, [](DeadlockState state) {
        {
            lock_guard<mutex> lock(deadlockMtx);
            currentDeadlockState = state;
        }
        logEvent("Deadlock " + state.status + ": "
                 + (state.action_taken.empty() ? "detected" : state.action_taken));
    });

    for (Worker* w : workers) w->start();
    detector.start();

    int uptimeSeconds = 0;

    thread(readInputFile, ref(scheduler)).detach();
    thread(writeStateFile, ref(scheduler), ref(resourceManager), ref(workers), ref(uptimeSeconds)).detach();
    thread(collectOutputs, ref(workers)).detach();

    cout << "All systems running. Waiting for file submissions..." << endl;
    while (true) this_thread::sleep_for(chrono::seconds(10));

    for (Worker* w : workers) { w->stop(); delete w; }
    detector.stop();
    return 0;
}