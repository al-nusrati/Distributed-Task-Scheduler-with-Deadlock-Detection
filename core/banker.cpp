#include "banker.h"

using namespace std;

bool Banker::isSafeState(const vector<Resource>&  resources, const map<string, map<string, int>>& allocation){
    map<string, int> available;
    for (const Resource& r : resources)
        available[r.id] = r.available;

    map<string, map<string, int>> need = allocation;

    map<string, bool> finished;
    for (const auto& [taskId, _] : allocation)
        finished[taskId] = false;

    int totalTasks   = allocation.size();
    int finishCount  = 0;

    bool progress = true;
    while (progress) {
        progress = false;

        for (const auto& [taskId, taskNeed] : need) {
            if (finished[taskId]) continue;

            if (canFinish(taskId, taskNeed, available)) {
                for (const auto& [rid, units] : allocation.at(taskId))
                    available[rid] += units;

                finished[taskId] = true;
                finishCount++;
                progress = true;
            }
        }
    }

    return finishCount == totalTasks;
}

bool Banker::canFinish(
    const string&            taskId,
    const map<string, int>&  need,
    const map<string, int>&  available)
{
    for (const auto& [rid, units] : need) {
        auto it = available.find(rid);
        if (it == available.end() || it->second < units)
            return false;
    }
    return true;
}