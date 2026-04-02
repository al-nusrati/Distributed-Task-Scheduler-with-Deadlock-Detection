#include<vector>
#include<string>
using namespace std;

enum class TaskStat
{
    Waiting,
    Processing,
    Completed,
    Denied      //here Banker algo will reject it
};

struct Task{
    string id;
    int priority;   //1-10
    int duration;   //seconds
    vector<string> resource_needed;
    TaskStat status;

    Task(string id, int priority, int duration, vector<string> resources);
    string statusToString() const;
};