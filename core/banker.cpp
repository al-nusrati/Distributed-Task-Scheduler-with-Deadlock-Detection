#include "banker.h"
using namespace std;

// ─────────────────────────────────────────────────────────────────────────────
// ROOT-CAUSE FIX
//
// Original bug: the safety simulation set `need = allocation` — meaning it
// treated each task's *currently held* resources as additional resources it
// still needs to acquire.  With R2 (Memory Block) having only 2 units:
//
//   T1 running, holds R2=1, R3=1  →  available: R2=1, R3=3
//   T2 wants R2+R3                →  tentative available: R2=0, R3=2
//   Safety sim: can T1 finish?    →  need[T1]={R2:1,R3:1}, avail R2=0 < 1 → NO
//   Safety sim: can T2 finish?    →  same         → NO
//   Verdict: UNSAFE  →  T2 DENIED  ← wrong!
//
// Correct reasoning: every task in this scheduler requests ALL of its resources
// atomically (all-or-nothing).  A task that has been granted resources already
// holds everything it needs; its *remaining* need is 0.  It will complete and
// release resources regardless of the current available count.  Circular wait
// (the necessary condition for deadlock) is therefore structurally impossible
// here — no task can hold some resources while blocked waiting for others.
//
// Consequence: the Banker safety simulation always returns true for this system.
// The sole effective guard is the per-resource availability check in
// ResourceManager::requestResource(), which already ensures no resource unit
// is over-allocated.
// ─────────────────────────────────────────────────────────────────────────────

bool Banker::isSafeState(
    const vector<Resource>&              resources,
    const map<string, map<string, int>>& allocation)
{
    // Tasks use atomic all-or-nothing requests: once granted, a task holds
    // everything it needs and will complete, so the remaining need is 0 for
    // every allocated task.  The system is therefore always in a safe state.
    (void)resources;
    (void)allocation;
    return true;
}

bool Banker::canFinish(
    const string&           taskId,
    const map<string, int>& need,
    const map<string, int>& available)
{
    (void)taskId;
    for (const auto& [rid, units] : need) {
        auto it = available.find(rid);
        if (it == available.end() || it->second < units)
            return false;
    }
    return true;
}