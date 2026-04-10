#pragma once

#include <string>
#include <vector>
#include <map>
#include "resource_manager.h"

using namespace std;



class Banker {
public:

    static bool isSafeState(
        const vector<Resource>&              resources,
        const map<string, map<string, int>>& allocation
    );

private:
    
    static bool canFinish(
        const string&                        taskId,
        const map<string, int>&              need,
        const map<string, int>&              available
    );
};