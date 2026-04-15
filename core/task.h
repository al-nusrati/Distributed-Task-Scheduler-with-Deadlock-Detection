#pragma once
#include<vector>
#include<string>
using namespace std;

enum class TaskStat {
    Waiting,
    Processing,
    Completed,
    Failed,     // compile error or runtime error
    Denied      // Banker's algo rejected
};

struct Task {
    string id;
    int    priority;        // auto-assigned by system (file size)
    int    duration;        // kept for compatibility
    string filename;        // original uploaded filename e.g. "main.cpp"
    string filepath;        // full path on disk e.g. "/uploads/T1_main.cpp"
    string language;        // "cpp", "python", "java", "c"
    string submitted_by;    // name entered by user on dashboard
    string output;          // captured stdout+stderr after execution
    vector<string> resources_needed;
    TaskStat status;
    string scheduling_mode; // "priority" or "round_robin" – user preference

    Task(string id, int priority, string filename, string filepath,
         string language, string submitted_by, vector<string> resources,
         string scheduling_mode = "priority");

    string statusToString() const;
};