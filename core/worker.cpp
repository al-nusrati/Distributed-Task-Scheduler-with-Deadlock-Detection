#include "worker.h"
#include <thread>
#include <chrono>
#include <array>
#include <cstdio>
using namespace std;

// ─────────────────────────────────────────────────────────────────────────────
// Platform-specific includes
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  define popen  popen
#  define pclose pclose
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / lifecycle
// ─────────────────────────────────────────────────────────────────────────────
Worker::Worker(string id,
               Scheduler& scheduler,
               ResourceManager& resourceManager,
               function<void(string)> onEventLog)
    : id(id), scheduler(scheduler), resourceManager(resourceManager),
      status(WorkerStatus::IDLE), currentTaskId(""), currentFile(""),
      currentUser(""), tasksCompleted(0), running(false), logEvent(onEventLog)
{}

void Worker::start() {
    running = true;
    int workerNum = 0;
    if (!id.empty() && isdigit(id.back()))
        workerNum = id.back() - '1';

    thread([this, workerNum]() {
        this_thread::sleep_for(chrono::milliseconds(workerNum * 150));
        run();
    }).detach();
}

void Worker::stop() { running = false; }

// ─────────────────────────────────────────────────────────────────────────────
// executeFile — two complete implementations:
//   • Windows: CreateProcess + dedicated reader thread + WaitForSingleObject
//              with a 10-second timeout + TerminateProcess if needed.
//   • Linux  : original popen approach (unchanged); "timeout 10" in the shell
//              command handles the limit there.
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32

string Worker::executeFile(Task& task) {

    // ── Build the command string (same logic as Linux, minus timeout prefix) ─
    string cmd;
    string outbin = task.filepath + ".exe";

    if (task.language == "cpp") {
        cmd = "g++ \"" + task.filepath + "\" -o \"" + outbin + "\" 2>&1 && \"" + outbin + "\" 2>&1";
    }
    else if (task.language == "c") {
        cmd = "gcc \"" + task.filepath + "\" -o \"" + outbin + "\" 2>&1 && \"" + outbin + "\" 2>&1";
    }
    else if (task.language == "python") {
        cmd = "python \"" + task.filepath + "\" 2>&1";
    }
    else if (task.language == "java") {
        string className = task.filename.substr(0, task.filename.find_last_of('.'));
        string dir       = task.filepath.substr(0, task.filepath.find_last_of('\\'));
        cmd = "cd \"" + dir + "\" && javac \"" + task.filepath
            + "\" 2>&1 && java -cp \"" + dir + "\" " + className + " 2>&1";
    }
    else if (task.language == "js") {
        cmd = "node \"" + task.filepath + "\" 2>&1";
    }
    else {
        return "Unsupported language: " + task.language;
    }

    // ── Set up an anonymous pipe for stdout + stderr ──────────────────────────
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;   // child inherits the write end

    HANDLE hRead  = nullptr;
    HANDLE hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        return "Failed to create pipe.";

    // Make sure our read end is NOT inherited by the child
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    // ── Launch child process ──────────────────────────────────────────────────
    STARTUPINFOA si{};
    si.cb         = sizeof(STARTUPINFOA);
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.dwFlags    = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};

    // cmd.exe /C <command>  — runs the whole pipeline (compile + execute)
    string fullCmd = "cmd.exe /C " + cmd;

    BOOL ok = CreateProcessA(
        nullptr,
        fullCmd.data(),   // mutable buffer required by the API
        nullptr, nullptr,
        TRUE,             // inherit handles (the write-end pipe)
        CREATE_NO_WINDOW, // don't flash a console window
        nullptr, nullptr,
        &si, &pi
    );

    // Close our copy of the write-end immediately — the child has its own copy.
    // When the child exits, both copies will be closed and ReadFile will see EOF.
    CloseHandle(hWrite);

    if (!ok) {
        CloseHandle(hRead);
        return "Failed to start process.";
    }

    // ── Drain output in a background thread ───────────────────────────────────
    // We read in parallel with WaitForSingleObject so output is never lost
    // even if the pipe buffer fills up (which would deadlock if we waited first).
    string    output;
    mutex     outMtx;

    thread reader([&]() {
        char   buf[512];
        DWORD  n = 0;
        while (ReadFile(hRead, buf, sizeof(buf) - 1, &n, nullptr) && n > 0) {
            buf[n] = '\0';
            lock_guard<mutex> lk(outMtx);
            output += buf;
        }
    });

    // ── Wait up to 10 seconds, then kill if still running ─────────────────────
    const DWORD TIMEOUT_MS = 10'000;
    bool timedOut = (WaitForSingleObject(pi.hProcess, TIMEOUT_MS) == WAIT_TIMEOUT);

    if (timedOut)
        TerminateProcess(pi.hProcess, 1);

    // After the process exits (or is killed), the child's write-end of the pipe
    // is closed and the reader thread will get ERROR_BROKEN_PIPE / returns false,
    // exiting its loop.
    reader.join();
    CloseHandle(hRead);

    // ── Collect exit code and clean up ────────────────────────────────────────
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (output.empty()) output = "(no output)";

    if (timedOut)
        output += "\n[Killed: exceeded 10s timeout]";
    else
        output += "\n[Exited with code " + to_string(static_cast<int>(exitCode)) + "]";

    return output;
}

#else // ──────────────────────────────────────────────────────── Linux / Docker

string Worker::executeFile(Task& task) {
    string cmd;
    string outbin = task.filepath + ".out";

    if (task.language == "cpp") {
        cmd = "g++ \"" + task.filepath + "\" -o \"" + outbin
            + "\" 2>&1 && timeout 10 \"" + outbin + "\" 2>&1";
    }
    else if (task.language == "c") {
        cmd = "gcc \"" + task.filepath + "\" -o \"" + outbin
            + "\" 2>&1 && timeout 10 \"" + outbin + "\" 2>&1";
    }
    else if (task.language == "python") {
        cmd = "timeout 10 python3 \"" + task.filepath + "\" 2>&1";
    }
    else if (task.language == "java") {
        string className = task.filename.substr(0, task.filename.find_last_of('.'));
        string dir       = task.filepath.substr(0, task.filepath.find_last_of('/'));
        cmd = "cd \"" + dir + "\" && javac \"" + task.filepath
            + "\" 2>&1 && timeout 10 java -cp \"" + dir + "\" " + className + " 2>&1";
    }
    else if (task.language == "js") {
        cmd = "timeout 10 node \"" + task.filepath + "\" 2>&1";
    }
    else {
        return "Unsupported language: " + task.language;
    }

    string output;
    array<char, 256> buffer;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "Failed to start process.";

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
        output += buffer.data();

    int exitCode = pclose(pipe);
    if (output.empty()) output = "(no output)";
    output += "\n[Exited with code " + to_string(exitCode) + "]";
    return output;
}

#endif // _WIN32

// ─────────────────────────────────────────────────────────────────────────────
// Worker main loop (identical on both platforms)
// ─────────────────────────────────────────────────────────────────────────────
void Worker::run() {
    while (running) {

        Task* task = scheduler.getNextTask();
        if (task == nullptr) {
            this_thread::sleep_for(chrono::milliseconds(500));
            continue;
        }

        bool granted = resourceManager.requestResource(task->id, task->resources_needed);
        if (!granted) {
            logEvent(id + " → Task " + task->id
                     + " (" + task->filename + ") denied — unsafe resource state");
            task->status = TaskStat::Denied;
            this_thread::sleep_for(chrono::milliseconds(200));
            scheduler.addTask(*task);
            delete task;
            this_thread::sleep_for(chrono::milliseconds(300));
            continue;
        }

        {
            lock_guard<mutex> lock(mtx);
            status        = WorkerStatus::BUSY;
            currentTaskId = task->id;
            currentFile   = task->filename;
            currentUser   = task->submitted_by;
            task->status  = TaskStat::Processing;
        }

        logEvent(id + " picked up Task " + task->id
                 + " (" + task->filename + ") from " + task->submitted_by);

        string output = executeFile(*task);

        bool failed = (output.find("[Exited with code 0]") == string::npos
                       && output.find("[Killed") == string::npos);
        // Note: a timed-out task is treated as "failed" intentionally — the
        // output already contains "[Killed: exceeded 10s timeout]".

        {
            lock_guard<mutex> lock(mtx);
            lastOutput = output;
            lastTaskId = task->id;
        }

        resourceManager.releaseResource(task->id);

        {
            lock_guard<mutex> lock(mtx);
            status        = WorkerStatus::IDLE;
            currentTaskId = "";
            currentFile   = "";
            currentUser   = "";
            tasksCompleted++;
        }

        if (failed)
            logEvent(id + " → Task " + task->id
                     + " (" + task->filename + ") FAILED — " + summarizeOutput(output));
        else
            logEvent(id + " completed Task " + task->id
                     + " (" + task->filename + ")");

        delete task;
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

string Worker::summarizeOutput(const string& output) {
    size_t pos  = output.find('\n');
    string first = (pos != string::npos) ? output.substr(0, pos) : output;
    if (first.size() > 80) first = first.substr(0, 80) + "...";
    return first;
}

WorkerState Worker::getState() const {
    lock_guard<mutex> lock(mtx);
    return {
        id,
        (status == WorkerStatus::BUSY) ? "busy" : "idle",
        currentTaskId,
        currentFile,
        currentUser,
        tasksCompleted,
        lastOutput,
        lastTaskId
    };
}