#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <mutex>
#include <deque>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "task.h"
#include "scheduler.h"
#include "resource_manager.h"
#include "banker.h"
#include "deadlock_detector.h"
#include "worker.h"
#include "nlohmann/json.hpp"

using namespace std;
using json = nlohmann::json;

// ============================================================
//  File Paths
// ============================================================
const string STATE_FILE  = "../../shared/state.json";
const string INPUT_FILE  = "../../shared/input.json";
const string UPLOADS_DIR = "../../shared/uploads/";

// ============================================================
//  Globals
// ============================================================
deque<json> eventLog;
mutex       eventLogMtx;
int         taskCounter = 0;

DeadlockState currentDeadlockState = { "none", "", {}, "" };
mutex         deadlockMtx;

// ============================================================
//  assignResources — based on language
//  cpp/c   → CPU Core + Memory Block
//  python  → Memory Block + I/O Channel
//  js      → I/O Channel + Network Socket
// ============================================================
vector<string> assignResources(const string& language) {
    if (language == "cpp" || language == "c")
        return { "R1", "R2" };
    else if (language == "python")
        return { "R2", "R3" };
    else if (language == "js")
        return { "R3", "R4" };
    return { "R1" };
}

// ============================================================
//  assignPriority — based on file size (Shortest Job First)
//
//  Smaller file = faster execution = higher priority
//
//  < 1KB   → priority 9  (highest)
//  < 5KB   → priority 7
//  < 20KB  → priority 5
//  < 50KB  → priority 3
//  50KB+   → priority 1  (lowest)
// ============================================================
int assignPriority(const string& filepath) {
    ifstream f(filepath, ios::binary | ios::ate);
    if (!f.is_open()) return 5; // default mid priority if unreadable

    long size = f.tellg();
    f.close();

    if (size < 1024)        return 9;
    else if (size < 5120)   return 7;
    else if (size < 20480)  return 5;
    else if (size < 51200)  return 3;
    else                    return 1;
}

// ============================================================
//  Timestamp
// ============================================================
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

// ============================================================
//  getLogType
// ============================================================
string getLogType(const string& msg) {
    if (msg.find("completed")  != string::npos) return "task_completed";
    if (msg.find("picked up")  != string::npos) return "task_started";
    if (msg.find("denied")     != string::npos) return "task_denied";
    if (msg.find("FAILED")     != string::npos) return "task_failed";
    if (msg.find("Deadlock")   != string::npos) return "deadlock_detected";
    if (msg.find("resolved")   != string::npos) return "deadlock_resolved";
    if (msg.find("added")      != string::npos) return "task_added";
    return "info";
}

// ============================================================
//  logEvent
// ============================================================
void logEvent(const string& message) {
    lock_guard<mutex> lock(eventLogMtx);
    json entry;
    entry["time"]    = getTimestamp();
    entry["type"]    = getLogType(message);
    entry["message"] = message;
    eventLog.push_front(entry);
    if (eventLog.size() > 50) eventLog.pop_back();
}

// ============================================================
//  readInputFile
//  Polls input.json every second
//
//  Expected input.json task format from Flask:
//  {
//    "filename"     : "main.cpp",
//    "language"     : "cpp",
//    "submitted_by" : "Jawad",
//    "file_content" : "..."   <- actual code as string
//  }
// ============================================================
void readInputFile(Scheduler& scheduler) {
    while (true) {
        this_thread::sleep_for(chrono::seconds(1));

        ifstream file(INPUT_FILE);
        if (!file.is_open()) continue;

        json data;
        try { file >> data; } catch (...) { continue; }
        file.close();

        if (!data.contains("pending_tasks")) continue;
        if (data["pending_tasks"].empty())   continue;

        for (auto& t : data["pending_tasks"]) {
            taskCounter++;
            string taskId  = "T" + to_string(taskCounter);
            string lang    = t.value("language",     "cpp");
            string fname   = t.value("filename",     "file." + lang);
            string user    = t.value("submitted_by", "Anonymous");
            string content = t.value("file_content", "");

            // Save file to uploads/
            string fpath = UPLOADS_DIR + taskId + "_" + fname;
            ofstream fout(fpath);
            fout << content;
            fout.close();

            // Auto-assign priority from file size (SJF)
            int priority = assignPriority(fpath);

            // Auto-assign resources from language
            vector<string> resources = assignResources(lang);

            Task newTask(taskId, priority, fname, fpath, lang, user, resources);
            scheduler.addTask(newTask);

            logEvent("Task " + taskId + " added — " + fname +
                     " by " + user +
                     " (priority " + to_string(priority) + ")");
        }

        // Clear after processing
        data["pending_tasks"] = json::array();
        ofstream out(INPUT_FILE);
        out << data.dump(2);
        out.close();
    }
}

// ============================================================
//  writeStateFile — every second
// ============================================================
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

        // Workers
        state["workers"] = json::array();
        for (Worker* w : workers) {
            WorkerState ws = w->getState();
            json worker;
            worker["id"]              = ws.id;
            worker["status"]          = ws.status;
            worker["current_task"]    = ws.current_task.empty() ? json(nullptr) : json(ws.current_task);
            worker["current_file"]    = ws.current_file.empty() ? json(nullptr) : json(ws.current_file);
            worker["current_user"]    = ws.current_user.empty() ? json(nullptr) : json(ws.current_user);
            worker["tasks_completed"] = ws.tasks_completed;
            state["workers"].push_back(worker);
        }

        // Task Queue
        state["task_queue"] = json::array();
        for (const Task& t : scheduler.getQueue()) {
            json task;
            task["id"]           = t.id;
            task["priority"]     = t.priority;
            task["filename"]     = t.filename;
            task["language"]     = t.language;
            task["submitted_by"] = t.submitted_by;
            task["status"]       = t.statusToString();
            state["task_queue"].push_back(task);
        }

        // Resources
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

        // Deadlock
        {
            lock_guard<mutex> lock(deadlockMtx);
            state["deadlock"]["status"]         = currentDeadlockState.status;
            state["deadlock"]["detected_at"]    = currentDeadlockState.detected_at.empty()
                                                    ? json(nullptr)
                                                    : json(currentDeadlockState.detected_at);
            state["deadlock"]["involved_tasks"] = currentDeadlockState.involved_tasks;
            state["deadlock"]["action_taken"]   = currentDeadlockState.action_taken.empty()
                                                    ? json(nullptr)
                                                    : json(currentDeadlockState.action_taken);
        }

        // Event Log
        {
            lock_guard<mutex> lock(eventLogMtx);
            state["event_log"] = json::array();
            for (const json& entry : eventLog)
                state["event_log"].push_back(entry);
        }

        ofstream out(STATE_FILE);
        out << state.dump(2);
        out.close();
    }
}

// ============================================================
//  main
// ============================================================
int main() {
    cout << "CoreSync — Starting..." << endl;

    Scheduler       scheduler;
    ResourceManager resourceManager;

    vector<Worker*> workers;
    for (int i = 1; i <= 4; i++) {
        Worker* w = new Worker(
            "W" + to_string(i),
            scheduler,
            resourceManager,
            logEvent
        );
        workers.push_back(w);
    }

    DeadlockDetector detector(resourceManager, [](DeadlockState state) {
        lock_guard<mutex> lock(deadlockMtx);
        currentDeadlockState = state;
        logEvent("Deadlock " + state.status + ": " +
                 (state.action_taken.empty() ? "detected" : state.action_taken));
    });

    for (Worker* w : workers) w->start();
    detector.start();

    int uptimeSeconds = 0;

    thread(readInputFile, ref(scheduler)).detach();
    thread(writeStateFile,
           ref(scheduler),
           ref(resourceManager),
           ref(workers),
           ref(uptimeSeconds)).detach();

    cout << "All systems running. Waiting for file submissions..." << endl;

    while (true)
        this_thread::sleep_for(chrono::seconds(10));

    for (Worker* w : workers) { w->stop(); delete w; }
    detector.stop();
    return 0;
}