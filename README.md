# OS Scheduler — Distributed Task Scheduler with Deadlock Detection

**NUST CEME | Computer Engineering | Operating Systems Project**  
**Group Members:** Jawad · Musab  

---

## What Is This Project?

This is a **cloud-hosted concurrent task scheduling system** that demonstrates core Operating Systems concepts in a real, working environment — not a simulation.

Tasks are submitted through a web dashboard. Multiple workers process them simultaneously. A deadlock prevention algorithm runs in the background ensuring no worker ever gets permanently stuck. Everything is live on Azure and accessible from any browser on any device.

---

## What Problem Does It Solve?

Imagine multiple workers sharing limited tools. Worker A holds a drill and needs a hammer. Worker B holds the hammer and needs the drill. Neither can proceed — this is a **deadlock**.

Our system:
- Assigns tasks to workers intelligently using scheduling algorithms
- Tracks which worker holds which resource
- Runs **Banker's Algorithm** before every resource grant to prevent deadlock
- Detects and recovers from deadlock automatically if it occurs
- Shows everything live on a web dashboard

---

## OS Concepts Covered

| Concept | Where It Appears |
|---|---|
| Process Synchronization | Mutex lock on shared task queue |
| Process Scheduling | Round Robin + Priority Scheduling |
| Deadlock Avoidance | Banker's Algorithm on resource requests |
| Deadlock Detection | Circular wait scan every 3 seconds |
| Deadlock Recovery | Lowest priority task terminated to break cycle |
| Parallel Execution | 4 worker threads running simultaneously |

---

## System Architecture

```
Browser (Any Device)
       |
       | HTTP
       ↓
    [Nginx]                          ← serves dashboard, forwards API calls
       |
       ↓
  [Flask API]                        ← translates browser requests to file operations
       |
       ↓
  [Shared Files]                     ← communication bridge
  ├── state.json     (C++ writes → Flask reads)
  ├── input.json     (Flask writes → C++ reads)
  └── scheduler_mode.txt (Flask writes → C++ reads)
       |
       ↓
  [C++ Core Binary]                  ← heart of the system
  ├── 4 Worker Threads
  ├── Scheduler (Round Robin / Priority)
  ├── Resource Manager
  ├── Banker's Algorithm
  └── Deadlock Detector

All of this runs inside Docker on an Azure VM.
Public URL → http://<azure-ip>
```

---

## How Communication Works

C++ and Flask cannot talk to each other directly. They communicate through three shared files:

### `shared/state.json`
- **Who writes:** C++ (every 1 second)
- **Who reads:** Flask
- **What it contains:** Complete current system state — workers, tasks, resources, deadlock status, event log
- Flask serves this to the dashboard on every `/api/status` call

### `shared/input.json`
- **Who writes:** Flask (when user submits a task via dashboard)
- **Who reads:** C++ (every 1 second)
- **What it contains:** Newly submitted tasks waiting to enter the queue
- C++ picks them up, adds to queue, clears the file

### `shared/scheduler_mode.txt`
- **Who writes:** Flask (when user toggles scheduler on dashboard)
- **Who reads:** C++ (every 1 second)
- **What it contains:** Either `priority` or `round_robin`
- C++ switches its scheduling algorithm accordingly

---

## Repository Structure

```
os-scheduler/
├── core/                  ← C++ - Jawad
│   ├── main.cpp
│   ├── task.h / task.cpp
│   ├── worker.h / worker.cpp
│   ├── scheduler.h / scheduler.cpp
│   ├── resource_manager.h / resource_manager.cpp
│   ├── banker.h / banker.cpp
│   ├── deadlock_detector.h / deadlock_detector.cpp
│   └── CMakeLists.txt
│
├── api/                   ← Flask - Musab
│   ├── app.py
│   └── requirements.txt
│
├── dashboard/             ← HTML/JS - Musab
│   ├── index.html
│   ├── style.css
│   └── app.js
│
├── shared/                ← Both read/write (never conflict — see rules below)
│   ├── state.json
│   ├── input.json
│   └── scheduler_mode.txt
│
├── infra/                 ← Docker + Nginx - Jawad
│   ├── docker-compose.yml
│   ├── Dockerfile.core
│   ├── Dockerfile.api
│   └── nginx.conf
│
└── README.md
```

### Shared File Ownership Rules
These rules prevent race conditions on the files themselves:

| File | Only Writer | Only Reader |
|---|---|---|
| state.json | C++ | Flask |
| input.json | Flask | C++ |
| scheduler_mode.txt | Flask | C++ |

**Never cross these boundaries.**

---

## API Endpoints (Flask)

Base URL: `http://<azure-ip>/api`

| Method | Endpoint | What It Does |
|---|---|---|
| GET | `/api/status` | Returns full state.json — called every second by dashboard |
| POST | `/api/task/add` | Adds new task to input.json — C++ picks it up |
| POST | `/api/scheduler/set` | Writes `priority` or `round_robin` to scheduler_mode.txt |
| GET | `/api/logs` | Returns event log from state.json |
| GET | `/api/deadlock` | Returns current deadlock status only |
| POST | `/api/system/reset` | Clears all tasks and resets system state |

### POST /api/task/add — Request Body
```json
{
  "priority": 7,
  "duration": 4,
  "resources_needed": ["R1", "R2"]
}
```

### POST /api/scheduler/set — Request Body
```json
{
  "mode": "round_robin"
}
```

---

## Dashboard (What the User Sees)

The dashboard auto-refreshes every second. No manual reload needed.

**Top Bar**
- Project name
- Scheduler toggle button (Priority ↔ Round Robin)
- System status indicator (Running / Idle)

**Workers Section**
- 4 boxes showing W1, W2, W3, W4
- Each shows: status (Busy/Idle), current task, tasks completed

**Task Queue Table**
- All pending and processing tasks
- Columns: ID, Priority, Duration, Resources Needed, Status
- Add Task button → opens a form with Priority, Duration, Resource checkboxes → Submit

**Resources Table**
- R1 CPU Core, R2 Memory Block, R3 I/O Channel, R4 Network Socket
- Columns: Total, Available, Held By

**Deadlock Alert**
- Hidden by default
- Red banner appears when deadlock is detected
- Shows which tasks are involved and what action was taken

**Event Log**
- Scrolling log at the bottom
- New events appear at top
- Icons: ✅ completed, 🔵 started, ⚠️ warning, 🔴 deadlock

**Reset Button**
- Clears all tasks, frees all resources, resets workers to idle

---

## How to Run Locally (Before Cloud Deployment)

### Prerequisites
- GCC/G++ with C++17 support (use WSL on Windows)
- CMake 3.16+
- Python 3
- pip

### Step 1 — Build C++ Core
```bash
cd core
mkdir build && cd build
cmake ..
make
```
This produces a `scheduler` binary.

### Step 2 — Install Flask Dependencies
```bash
cd api
pip install -r requirements.txt
```

### Step 3 — Run Everything
```bash
# Terminal 1
./core/build/scheduler

# Terminal 2
python api/app.py

# Terminal 3
open browser at http://localhost:5000
```

---

## How to Run with Docker (Recommended)

From the root of the repo:
```bash
docker-compose -f infra/docker-compose.yml up
```

Open browser at `http://localhost:8080`

Everything runs from one command. No separate terminals needed.

---

## Cloud Deployment (Azure VM)

```bash
# On Azure VM after SSH
git clone https://github.com/<your-repo>/os-scheduler.git
cd os-scheduler
sudo apt install docker.io docker-compose -y
docker-compose -f infra/docker-compose.yml up -d
```

Then open port 80 in Azure Network Security Group.

Dashboard live at: `http://<your-vm-public-ip>`

### To update after a code change:
```bash
# SSH into VM
git pull
docker-compose -f infra/docker-compose.yml restart
```

---

## Why Cloud Makes This Project Different

Without cloud, concurrent load is artificial — you manufacture it yourself.

With cloud, multiple real people hitting the public URL simultaneously creates genuine concurrent HTTP requests. Your mutex locks protect against real race conditions. Your Banker's Algorithm responds to real simultaneous resource requests. The deadlock detector runs under actual system pressure.

**Cloud does not just host the project. It makes the problem real.**

---

## Division of Work

| Component | Owner |
|---|---|
| C++ Core (all logic) | Jawad |
| Azure VM + Nginx | Jawad |
| Docker + docker-compose | Jawad |
| Flask API | Musab |
| Dashboard (HTML/JS) | Musab |

---
