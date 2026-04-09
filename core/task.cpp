#include "task.h"
using namespace std;

Task::Task(string id, int priority, string filename, string filepath, string language, string submitted_by, vector<string> resources)
    : id(id),priority(priority),duration(0),filename(filename),filepath(filepath),language(language),submitted_by(submitted_by),output(""),resources_needed(resources),status(TaskStat::Waiting)
{}

string Task::statusToString() const {
    switch (status) {
        case TaskStat::Waiting:    return "waiting";
        case TaskStat::Processing: return "processing";
        case TaskStat::Completed:  return "completed";
        case TaskStat::Failed:     return "failed";
        case TaskStat::Denied:     return "denied";
        default:                   return "unknown";
    }
}