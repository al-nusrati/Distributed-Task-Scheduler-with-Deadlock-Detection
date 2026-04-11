#include "resource_manager.h"
#include "banker.h"

using namespace std;

ResourceManager::ResourceManager() {
    resources = {
        { "R1", "CPU Core",       3, 3, {} },
        { "R2", "Memory Block",   2, 2, {} },
        { "R3", "I/O Channel",    4, 4, {} },
        { "R4", "Network Socket", 2, 2, {} }
    };
}

bool ResourceManager::requestResource(const string& taskId, const vector<string>& needed) {
    lock_guard<mutex> lock(mtx);

    for (const string& rid : needed) {
        for (Resource& r : resources) {
            if (r.id == rid && r.available < 1)
                return false;
        }
    }

    for (const string& rid : needed) {
        for (Resource& r : resources) {
            if (r.id == rid) {
                r.available--;
                r.held_by.push_back(taskId);
                allocation[taskId][rid]++;
            }
        }
    }

    bool safe = Banker::isSafeState(resources, allocation);

    if (!safe) {
        for (const string& rid : needed) {
            for (Resource& r : resources) {
                if (r.id == rid) {
                    r.available++;
                    for (auto it = r.held_by.begin(); it != r.held_by.end(); ) {
                        if (*it == taskId) it = r.held_by.erase(it);
                        else ++it;
                    }
                }
            }
        }
        allocation.erase(taskId);
        return false;
    }

    return true;
}

void ResourceManager::releaseResource(const string& taskId) {
    {
        lock_guard<mutex> lock(mtx);

        if (allocation.find(taskId) == allocation.end()) return;

        for (auto& [rid, units] : allocation[taskId]) {
            for (Resource& r : resources) {
                if (r.id == rid) {
                    r.available += units;
                    for (auto it = r.held_by.begin(); it != r.held_by.end(); ) {
                        if (*it == taskId) it = r.held_by.erase(it);
                        else ++it;
                    }
                }
            }
        }
        allocation.erase(taskId);
    }
    // Wake ALL waiting workers immediately when resources are freed
    cv.notify_all();
}

void ResourceManager::waitForResources(int timeoutMs) {
    unique_lock<mutex> lock(mtx);
    cv.wait_for(lock, chrono::milliseconds(timeoutMs));
}

int ResourceManager::getAvailable(const string& resourceId) const {
    lock_guard<mutex> lock(mtx);
    for (const Resource& r : resources)
        if (r.id == resourceId) return r.available;
    return 0;
}

int ResourceManager::getTotal(const string& resourceId) const {
    lock_guard<mutex> lock(mtx);
    for (const Resource& r : resources)
        if (r.id == resourceId) return r.total;
    return 0;
}

vector<string> ResourceManager::getHeldBy(const string& resourceId) const {
    lock_guard<mutex> lock(mtx);
    for (const Resource& r : resources)
        if (r.id == resourceId) return r.held_by;
    return {};
}

vector<Resource> ResourceManager::getAllResources() const {
    lock_guard<mutex> lock(mtx);
    return resources;
}

map<string, map<string, int>> ResourceManager::getAllocationMatrix() const {
    lock_guard<mutex> lock(mtx);
    return allocation;
}