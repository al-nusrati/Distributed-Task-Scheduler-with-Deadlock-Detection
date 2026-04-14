# Distributed Task Scheduler (Docker + Flask + C++ Core)

## 🧠 Overview
This project implements a distributed-style task scheduler system that mimics how an operating system schedules and executes tasks. It integrates a web-based interface, a Python Flask API, and a high-performance C++ backend scheduler.

The system allows users to submit code files, which are then processed and executed by a C++ scheduling engine, with real-time updates reflected on a dashboard.

---

## 🏗️ Architecture

Browser (Frontend UI)
        ↓
Flask API (Python)
        ↓
input.json (shared communication file)
        ↓
C++ Scheduler (Core Engine)
        ↓
state.json (system state + logs)
        ↓
Frontend Dashboard (polling API)

---

## ⚙️ Tech Stack

### 🖥️ Frontend
- HTML
- CSS
- JavaScript (Fetch API)

### 🔙 Backend
- Python (Flask)
- Flask-CORS

### ⚡ Core Engine
- C++
- CMake / Make

### 📦 Containerization
- Docker
- Docker Compose

### 📂 Data Communication
- JSON (input.json, state.json)

---

## 🔄 Working of the System

### 1. User Input
User provides:
- File path of code (inside Docker container)
- User name

### 2. Flask API
- Receives request
- Reads file content from given path
- Detects language automatically
- Writes structured task into `input.json`

### 3. C++ Scheduler
- Continuously monitors `input.json`
- Picks tasks
- Executes them (multi-threaded)
- Handles:
  - Success
  - Runtime errors
  - Compilation errors
  - Deadlock detection (planned)
- Updates `state.json`

### 4. Frontend Dashboard
- Polls `/api/status`
- Displays:
  - Workers status
  - Event logs
  - Task outputs
  - System metrics

---

## ✅ Features Implemented

✔ Dockerized full system  
✔ Flask API for task submission  
✔ C++ scheduler integration  
✔ Shared JSON communication  
✔ Multi-language task support:
- Python
- C++
- C
- JavaScript (planned runtime support)

✔ Event logging system  
✔ Worker-based execution model  
✔ Real-time dashboard updates  

---

## ⚠️ Current Status

🚧 The system is mostly integrated, but:

- Task submission via frontend is currently **failing with a 400 BAD REQUEST error**
- Issue is related to mismatch between frontend payload and backend expectations (`file_path`)
- Frontend DOM element access is also causing errors

👉 This step is **NOT fully completed yet** and requires debugging.

---

## 🚀 Future Improvements

- Fix frontend request handling (current blocker)
- Add file upload feature (instead of manual path)
- Add sandboxed execution environment
- Improve UI/UX of dashboard
- Add priority-based scheduling visualization
- Add deadlock detection & recovery mechanisms
- Add Node.js support inside Docker

---

## 📌 Key Learning Outcomes

- Integration of multiple technologies (Python + C++ + Docker)
- Inter-process communication using JSON
- Real-world scheduling system simulation
- Debugging distributed systems
- Container-based development

---

## 🧪 How to Run

```bash
docker compose up --build
```

Then open:
```
http://localhost
```

---

## 👨‍💻 Author

Musab Farooq

---

## 💡 Note

This project is a learning-based implementation of a distributed scheduler and is still under active development.
