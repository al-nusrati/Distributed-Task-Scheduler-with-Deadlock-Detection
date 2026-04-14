#pragma once
#include<vector>
#include<mutex>
#include<string>
#include<map>

using namespace std;

struct Resource {
    string id;
    string name;
    int total;
    int available;
    vector<string> held_by;
};

class ResourceManager{
private:
    
    vector<Resource>               resources;
    map<string, map<string, int>>  allocation; // who holds what
    mutable mutex                  mtx;        

public:
    ResourceManager();

    bool requestResource(const string& taskId, const vector<string>& needed);
    void releaseResource(const string &taskId);
    int getAvailable(const string& resourceId) const;
    int getTotal(const string& resourceId) const;

    vector<string> getHeldBy(const string& resourceId) const;
    vector<Resource> getAllResources() const;

    map<string, map<string, int>> getAllocationMatrix() const;
};
