const API = "/api";

// ─────────────────────────────────────────────────────────────────────────────
// Force all fetch requests to include credentials (cookies) so the session
// is sent to Flask.
// ─────────────────────────────────────────────────────────────────────────────
const originalFetch = window.fetch;
window.fetch = function(...args) {
  if (typeof args[1] === 'object') {
    args[1].credentials = 'include';
  } else {
    args[1] = { credentials: 'include' };
  }
  return originalFetch.apply(this, args);
};

let refreshMs = 1000;
let poller = null;
let selectedTaskId = null;
let currentMode = "priority";
let currentUser = null;

const ganttPalette = [
  ["#7EC8FF", "#4C92FF"],
  ["#3DE2D0", "#4C92FF"],
  ["#FFB56B", "#FF7E56"],
  ["#B79CFF", "#6C78FF"],
  ["#7BF0A8", "#39BFA6"],
  ["#F89CD1", "#9C7EFF"]
];

function qs(id) { return document.getElementById(id); }

function formatSeconds(totalSeconds) {
  const seconds = Math.max(0, Number(totalSeconds || 0));
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = Math.floor(seconds % 60);
  if (h > 0) return `${h}h ${m}m ${s}s`;
  if (m > 0) return `${m}m ${s}s`;
  return `${s}s`;
}

function showBanner(message, kind = "success") {
  const banner = qs("banner");
  banner.className = `banner ${kind}`;
  banner.textContent = message;
  setTimeout(() => banner.classList.add("hidden"), 2600);
}

function parseTimeToSeconds(timeText) {
  if (!timeText || typeof timeText !== "string") return null;
  const [h, m, s] = timeText.split(":").map(Number);
  if ([h, m, s].some(Number.isNaN)) return null;
  return h * 3600 + m * 60 + s;
}

function normalizeMode(mode) {
  return String(mode || "priority").replace("_", " ");
}

function labelStatus(status) {
  return String(status || "unknown").replace("_", " ");
}

function hasOutput(task, outputs) {
  return Boolean(outputs?.[task?.id] || task?.output);
}

function getWorkerId(value, fallback = "W?") {
  if (value === null || value === undefined || value === "") return fallback;
  return String(value).startsWith("W") ? String(value) : `W${value}`;
}

function buildTaskRegistry(data) {
  const taskMap = new Map();
  const queue = Array.isArray(data.task_queue) ? data.task_queue : Array.isArray(data.queue) ? data.queue : [];
  const outputs = data.task_outputs || {};
  const workers = data.workers || [];
  const events = data.event_log || data.logs || [];

  function ensureTask(id, seed = {}) {
    const taskId = id || seed.id || seed.task_id || `unknown_${taskMap.size + 1}`;
    if (!taskMap.has(taskId)) {
      taskMap.set(taskId, {
        id: taskId,
        filename: seed.filename || seed.current_file || seed.file || "(unknown)",
        submittedBy: seed.submitted_by || seed.current_user || seed.user || "-",
        priority: seed.priority ?? null,
        workerId: seed.workerId || null,
        status: seed.status || "waiting",
        output: seed.output || "",
        start: seed.start ?? null,
        end: seed.end ?? null,
        logTime: seed.logTime ?? null,
        schedulingMode: seed.scheduling_mode || "priority"
      });
    }
    return taskMap.get(taskId);
  }

  queue.forEach((task) => {
    const entry = ensureTask(task.id || task.task_id, task);
    entry.filename = task.filename || entry.filename;
    entry.submittedBy = task.submitted_by || task.user || entry.submittedBy;
    entry.priority = task.priority ?? entry.priority;
    entry.status = task.status || "waiting";
    entry.schedulingMode = task.scheduling_mode || entry.schedulingMode;
  });

  Object.entries(outputs).forEach(([taskId, output]) => {
    const entry = ensureTask(taskId, { output });
    entry.output = output;
  });

  workers.forEach((worker) => {
    const workerId = getWorkerId(worker.id);
    if (worker.current_task) {
      const entry = ensureTask(worker.current_task, {
        filename: worker.current_file,
        submitted_by: worker.current_user,
        workerId,
        status: "running"
      });
      entry.workerId = workerId;
      entry.filename = worker.current_file || entry.filename;
      entry.submittedBy = worker.current_user || entry.submittedBy;
      entry.status = "running";
    }

    if (worker.last_task_id) {
      const entry = ensureTask(worker.last_task_id, {
        filename: worker.current_file || worker.last_task_id,
        workerId,
        status: "completed",
        output: worker.last_output || ""
      });
      entry.workerId = entry.workerId || workerId;
      entry.output = entry.output || worker.last_output || "";
      if (entry.status !== "running") entry.status = "completed";
    }
  });

  events.forEach((evt) => {
    const message = evt?.message || "";
    const t = parseTimeToSeconds(evt?.time);
    let match = message.match(/^Task\s+(T\d+)\s+added.*\((?:priority\s+)?(\d+)\)/i);
    if (match) {
      const [, taskId, priority] = match;
      const entry = ensureTask(taskId);
      entry.priority = Number(priority);
      entry.logTime = t ?? entry.logTime;
      return;
    }
    match = message.match(/^(W\d+)\s+picked up Task\s+(T\d+)\s+\(([^)]+)\)\s+from\s+(.+)$/i);
    if (match) {
      const [, workerId, taskId, filename, user] = match;
      const entry = ensureTask(taskId, { filename, submitted_by: user });
      entry.workerId = workerId;
      entry.filename = filename;
      entry.submittedBy = user;
      entry.status = "running";
      entry.start = t ?? entry.start;
      entry.logTime = t ?? entry.logTime;
      return;
    }
    match = message.match(/^(W\d+)\s+completed Task\s+(T\d+)\s+\(([^)]+)\)/i);
    if (match) {
      const [, workerId, taskId, filename] = match;
      const entry = ensureTask(taskId, { filename });
      entry.workerId = workerId;
      entry.filename = filename;
      entry.status = "completed";
      entry.end = t ?? entry.end;
      entry.logTime = t ?? entry.logTime;
      return;
    }
    match = message.match(/^(W\d+).+Task\s+(T\d+)\s+\(([^)]+)\)\s+FAILED/i);
    if (match) {
      const [, workerId, taskId, filename] = match;
      const entry = ensureTask(taskId, { filename });
      entry.workerId = workerId;
      entry.filename = filename;
      entry.status = "failed";
      entry.end = t ?? entry.end;
      entry.logTime = t ?? entry.logTime;
      return;
    }
    match = message.match(/^(W\d+).+Task\s+(T\d+)\s+\(([^)]+)\)\s+denied/i);
    if (match) {
      const [, workerId, taskId, filename] = match;
      const entry = ensureTask(taskId, { filename });
      entry.workerId = workerId;
      entry.filename = filename;
      entry.status = "denied";
      entry.logTime = t ?? entry.logTime;
    }
  });

  const tasks = Array.from(taskMap.values());
  tasks.sort((a, b) => (b.logTime ?? b.end ?? b.start ?? 0) - (a.logTime ?? a.end ?? a.start ?? 0));
  return tasks;
}

function buildGanttData(data, tasks) {
  const rows = {};
  const workers = data.workers || [];
  const workerIds = workers.map((worker) => getWorkerId(worker.id));
  workerIds.forEach((id) => { rows[id] = []; });

  let minTime = null, maxTime = null;

  tasks.forEach((task, index) => {
    if (task.start === null && task.end === null) return;
    const start = task.start ?? (task.end !== null ? task.end - 1 : null);
    const end = task.end ?? (start !== null ? start + 1 : null);
    if (start === null || end === null) return;

    const workerId = task.workerId || workerIds[index % Math.max(workerIds.length, 1)] || "W1";
    if (!rows[workerId]) rows[workerId] = [];
    rows[workerId].push({
      taskId: task.id,
      filename: task.filename,
      status: task.status,
      start,
      end: end <= start ? start + 1 : end
    });

    minTime = minTime === null ? start : Math.min(minTime, start);
    maxTime = maxTime === null ? end : Math.max(maxTime, end);
  });

  Object.keys(rows).forEach((workerId) => { rows[workerId].sort((a, b) => a.start - b.start); });
  return { rows, minTime: minTime ?? 0, maxTime: maxTime ?? 1 };
}

function createTaskChip(task, outputs) {
  const item = document.createElement("li");
  const button = document.createElement("button");
  button.className = `task-chip ${selectedTaskId === task.id ? "active" : ""}`;
  button.innerHTML = `
    <div class="task-chip-top">
      <strong>${task.id}</strong>
      <span class="chip-status ${task.status}">${labelStatus(task.status)}</span>
    </div>
    <span>${task.filename}</span>
    <small>${task.submittedBy} · priority ${task.priority ?? "-"} · ${task.schedulingMode}</small>
  `;
  button.onclick = () => {
    selectedTaskId = task.id;
    renderOutput(task, outputs);
    refreshTaskSelectionStyles();
  };
  item.appendChild(button);
  return item;
}

function renderOutput(task, outputs) {
  const taskId = typeof task === "string" ? task : task?.id;
  const taskMeta = typeof task === "string" ? { id: task } : task || {};
  const output = outputs?.[taskId] || taskMeta.output || "(No output captured yet)";
  qs("taskMeta").textContent =
    `Task ${taskId} · ${taskMeta.filename || "(unknown file)"} · ${taskMeta.submittedBy || "-"} · ${labelStatus(taskMeta.status || "unknown")}`;
  qs("taskOutput").textContent = output;
}

function refreshTaskSelectionStyles() {
  document.querySelectorAll(".task-chip").forEach((chip) => {
    const strong = chip.querySelector("strong");
    const isActive = strong && strong.textContent === selectedTaskId;
    chip.classList.toggle("active", isActive);
  });
}

function choosePreferredTask(tasks, outputs) {
  const selectedTask = tasks.find((task) => task.id === selectedTaskId);
  if (selectedTask && hasOutput(selectedTask, outputs)) return selectedTask;
  const latestWithOutput = tasks.find((task) => hasOutput(task, outputs));
  if (latestWithOutput) {
    selectedTaskId = latestWithOutput.id;
    return latestWithOutput;
  }
  return selectedTask || null;
}

function renderModeButtons(mode) {
  currentMode = mode;
  qs("modePriorityBtn").classList.toggle("active-mode", mode === "priority");
  qs("modeRoundRobinBtn").classList.toggle("active-mode", mode === "round_robin");
  qs("modeBadge").textContent = normalizeMode(mode);
}

function renderWorkers(workers) {
  const host = qs("workers");
  host.innerHTML = "";
  const busyCount = (workers || []).filter((worker) => String(worker.status).toLowerCase() === "busy").length;
  qs("workerSummary").textContent = `${busyCount}/${workers.length || 0} active`;

  (workers || []).forEach((worker) => {
    const card = document.createElement("article");
    card.className = "worker-card";
    const status = String(worker.status || "idle").toLowerCase();
    card.innerHTML = `
      <div class="worker-head">
        <h3>${getWorkerId(worker.id)}</h3>
        <span class="status ${status}">${labelStatus(status)}</span>
      </div>
      <p><strong>${worker.tasks_completed ?? 0}</strong> tasks completed</p>
      <p>Current: ${worker.current_task || "-"}</p>
      <p>File: ${worker.current_file || worker.last_task_id || "-"}</p>
    `;
    host.appendChild(card);
  });
}

function renderResources(resources) {
  const host = qs("resources");
  host.innerHTML = "";
  (resources || []).forEach((rsc) => {
    const used = Math.max(0, (rsc.total || 0) - (rsc.available || 0));
    const pct = rsc.total > 0 ? (used / rsc.total) * 100 : 0;
    const row = document.createElement("div");
    row.className = "resource-item";
    row.innerHTML = `
      <div class="resource-head">
        <span>${rsc.id} · ${rsc.name}</span>
        <span>${used}/${rsc.total} used</span>
      </div>
      <div class="resource-bar"><div class="resource-fill" style="width: ${pct}%"></div></div>
    `;
    host.appendChild(row);
  });
}

function renderInputStatus(inputStatus) {
  const pendingCount = Number(inputStatus?.pending_count || 0);
  const processingEnabled = Boolean(inputStatus?.processing_enabled);
  qs("pendingTasksMeta").textContent =
    pendingCount > 0
      ? `${pendingCount} task${pendingCount === 1 ? "" : "s"} queued in input.json${processingEnabled ? " and being consumed" : ". Press Start to run them."}`
      : "No pending input tasks.";
  qs("inputStatusBadge").textContent = processingEnabled ? "processing on" : `queued ${pendingCount}`;
  qs("startProcessingBtn").disabled = pendingCount === 0 || processingEnabled;
}

function renderTasks(tasks, outputs) {
  qs("queue").innerHTML = "";
  qs("running").innerHTML = "";
  qs("completed").innerHTML = "";

  // Filter to current user only (case‑insensitive)
  const userTasks = currentUser
    ? tasks.filter(t => t.submittedBy && t.submittedBy.trim().toLowerCase() === currentUser.toLowerCase())
    : tasks;

  const waiting = userTasks.filter(t => t.status === "waiting" || t.status === "denied");
  const running = userTasks.filter(t => t.status === "running");
  const completed = userTasks.filter(t => t.status === "completed" || t.status === "failed");

  waiting.forEach(t => qs("queue").appendChild(createTaskChip(t, outputs)));
  running.forEach(t => qs("running").appendChild(createTaskChip(t, outputs)));
  completed.forEach(t => qs("completed").appendChild(createTaskChip(t, outputs)));

  if (!userTasks.length) {
    qs("queue").innerHTML = "<li class=\"gantt-empty\">No tasks submitted by you yet.</li>";
  }

  const preferredTask = choosePreferredTask(userTasks, outputs);
  if (preferredTask) {
    renderOutput(preferredTask, outputs);
    refreshTaskSelectionStyles();
  }
}

function renderLogs(eventLog) {
  const host = qs("logs");
  host.innerHTML = "";
  (eventLog || []).forEach((evt) => {
    const item = document.createElement("div");
    item.className = "log-item";
    item.innerHTML = `<small>${evt?.time || "-"}</small><strong>${labelStatus(evt?.type || "event")}</strong><div>${evt?.message || String(evt)}</div>`;
    host.appendChild(item);
  });
}

function renderAxis(minTime, maxTime) {
  const axis = qs("ganttAxis");
  axis.innerHTML = "";
  const spacer = document.createElement("div"); spacer.className = "axis-spacer";
  const track = document.createElement("div"); track.className = "axis-track";
  const totalWindow = Math.max(1, maxTime - minTime);
  for (let i = 0; i < 4; i++) {
    const seconds = minTime + Math.round((totalWindow * i) / 3);
    const label = document.createElement("span");
    label.textContent = formatSeconds(seconds);
    track.appendChild(label);
  }
  axis.appendChild(spacer);
  axis.appendChild(track);
}

function renderGantt(data, tasks, outputs) {
  const gantt = qs("gantt");
  gantt.innerHTML = "";
  const userTasks = currentUser
    ? tasks.filter(t => t.submittedBy && t.submittedBy.trim().toLowerCase() === currentUser.toLowerCase())
    : tasks;
  const { rows, minTime, maxTime } = buildGanttData(data, userTasks);
  const totalWindow = Math.max(1, maxTime - minTime);
  qs("ganttWindow").textContent = `Window ${formatSeconds(totalWindow)}`;
  renderAxis(minTime, maxTime);

  const workerIds = Object.keys(rows);
  const hasBars = workerIds.some((workerId) => rows[workerId].length);
  if (!hasBars) {
    const empty = document.createElement("div");
    empty.className = "gantt-empty";
    empty.textContent = "No execution bars for your tasks yet.";
    gantt.appendChild(empty);
    return;
  }

  workerIds.forEach((workerId) => {
    const track = document.createElement("div"); track.className = "gantt-track";
    const workerLabel = document.createElement("div"); workerLabel.className = "gantt-worker"; workerLabel.textContent = workerId;
    const row = document.createElement("div"); row.className = "gantt-row";

    rows[workerId].forEach((task, index) => {
      const bar = document.createElement("button");
      const left = ((task.start - minTime) / totalWindow) * 100;
      const width = Math.max(8, ((task.end - task.start) / totalWindow) * 100);
      const colors = ganttPalette[index % ganttPalette.length];
      bar.className = `gantt-bar ${task.status === "running" ? "running" : "completed"}`;
      bar.style.left = `${left}%`;
      bar.style.width = `${width}%`;
      if (task.status !== "running") {
        bar.style.background = `linear-gradient(135deg, ${colors[0]}, ${colors[1]})`;
      }
      bar.textContent = `${task.taskId} · ${task.filename}`;
      bar.title = `${task.taskId} (${task.filename}) · ${formatSeconds(task.end - task.start)}`;
      bar.onclick = () => {
        selectedTaskId = task.taskId;
        const selectedTask = userTasks.find((entry) => entry.id === task.taskId) || task;
        renderOutput(selectedTask, outputs);
        refreshTaskSelectionStyles();
      };
      row.appendChild(bar);
    });
    track.appendChild(workerLabel);
    track.appendChild(row);
    gantt.appendChild(track);
  });
}

function renderDeadlock(deadlock) {
  const status = deadlock?.status || "none";
  const isAlert = status !== "none";
  qs("deadlockCard").classList.toggle("alert", isAlert);
  qs("deadlockStatus").textContent = isAlert ? "Deadlock detected" : "No deadlock";
  qs("deadlockDetails").textContent = isAlert
    ? `At ${deadlock.detected_at || "unknown"} · tasks: ${(deadlock.involved_tasks || []).join(", ")}`
    : "System stable.";
}

function renderStats(data, mode, tasks) {
  const system = data.system || {};
  const userTasks = currentUser
    ? tasks.filter(t => t.submittedBy && t.submittedBy.trim().toLowerCase() === currentUser.toLowerCase())
    : tasks;
  const queueCount = userTasks.filter(t => t.status === "waiting" || t.status === "denied").length;
  const runningCount = userTasks.filter(t => t.status === "running").length;
  const completedCount = userTasks.filter(t => t.status === "completed" || t.status === "failed").length;

  qs("statStatus").textContent = labelStatus(system.status || "unknown");
  qs("statCompleted").textContent = String(system.total_tasks_completed ?? completedCount);
  qs("statQueue").textContent = String(queueCount);
  qs("statRunning").textContent = String(runningCount);
  qs("statUptime").textContent = formatSeconds(system.uptime_seconds || 0);
  qs("statusNarrative").textContent =
    `${completedCount} completed, ${runningCount} running, ${queueCount} waiting under ${normalizeMode(mode)} scheduling.`;
}

async function addJob() {
  const submitButton = qs("submitTaskBtn");
  const fileInput = qs("fileInput");
  const schedulingMode = qs("scheduling_mode").value;
  const file = fileInput.files[0];
  if (!file) {
    showBanner("Please select a file to upload.", "error");
    return;
  }

  submitButton.disabled = true;
  try {
    const formData = new FormData();
    formData.append("file", file);
    formData.append("scheduling_mode", schedulingMode);

    const res = await fetch(`${API}/upload`, { method: "POST", body: formData });
    const data = await res.json();
    if (!res.ok) {
      showBanner(data.error || "Upload failed.", "error");
      return;
    }
    showBanner("File uploaded and task queued.");
    fileInput.value = "";
    await refreshAll();
  } catch (error) {
    showBanner(`Upload failed: ${error.message}`, "error");
  } finally {
    submitButton.disabled = false;
  }
}

async function startProcessing() {
  const startButton = qs("startProcessingBtn");
  startButton.disabled = true;
  try {
    const res = await fetch(`${API}/input/start`, { method: "POST" });
    const data = await res.json();
    if (!res.ok) {
      showBanner(data.error || "Start failed.", "error");
      return;
    }
    showBanner("Scheduler started reading input.json.");
    await refreshAll();
  } catch (error) {
    showBanner(`Start failed: ${error.message}`, "error");
  } finally {
    startButton.disabled = false;
  }
}

async function setMode(mode) {
  try {
    const res = await fetch(`${API}/scheduler/mode`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ mode })
    });
    const data = await res.json();
    if (!res.ok) {
      showBanner(data.error || "Mode switch failed.", "error");
      return;
    }
    renderModeButtons(mode);
    showBanner(`Global scheduler mode set to ${mode}. (Per‑task policies still apply)`);
    await refreshAll();
  } catch (error) {
    showBanner(`Mode switch failed: ${error.message}`, "error");
  }
}

async function logout() {
  await fetch(`${API}/logout`, { method: "POST" });
  window.location.href = "/login.html";
}

async function fetchMode() {
  const res = await fetch(`${API}/scheduler/mode`);
  if (!res.ok) throw new Error(`Mode API returned ${res.status}`);
  const data = await res.json();
  renderModeButtons(data.mode || "priority");
  return data.mode;
}

async function fetchStatus() {
  const res = await fetch(`${API}/status`);
  if (!res.ok) throw new Error(`Status API returned ${res.status}`);
  return res.json();
}

async function fetchInputStatus() {
  const res = await fetch(`${API}/input/status`);
  if (!res.ok) throw new Error(`Input status API returned ${res.status}`);
  return res.json();
}

async function checkAuth() {
  const res = await fetch(`${API}/current_user`);
  if (!res.ok) {
    window.location.href = "/login.html";
    return;
  }
  const data = await res.json();
  currentUser = data.user;
  qs("userDisplay").textContent = currentUser || "Unknown";
}

async function refreshAll() {
  try {
    if (!currentUser) await checkAuth();
    const [mode, status, inputStatus] = await Promise.all([fetchMode(), fetchStatus(), fetchInputStatus()]);
    const tasks = buildTaskRegistry(status);
    renderStats(status, mode, tasks);
    renderInputStatus(inputStatus);
    renderWorkers(status.workers || []);
    renderResources(status.resources || []);
    renderTasks(tasks, status.task_outputs || {});
    renderLogs(status.event_log || status.logs || []);
    renderDeadlock(status.deadlock || {});
    renderGantt(status, tasks, status.task_outputs || {});
    qs("lastUpdated").textContent = `Updated ${new Date().toLocaleTimeString()}`;
    qs("banner").classList.add("hidden");
  } catch (error) {
    if (error.message.includes("401") || error.message.includes("Unauthorized")) {
      window.location.href = "/login.html";
    } else {
      showBanner(`Live update failed: ${error.message}`, "error");
    }
  }
}

function restartPolling() {
  if (poller) clearInterval(poller);
  poller = setInterval(refreshAll, refreshMs);
}

function bindEvents() {
  qs("submitTaskBtn").addEventListener("click", addJob);
  qs("startProcessingBtn").addEventListener("click", startProcessing);
  qs("modePriorityBtn").addEventListener("click", () => setMode("priority"));
  qs("modeRoundRobinBtn").addEventListener("click", () => setMode("round_robin"));
  qs("logoutBtn").addEventListener("click", logout);
  qs("refreshRate").addEventListener("input", (e) => {
    refreshMs = Number(e.target.value || 1000);
    qs("refreshLabel").textContent = `${refreshMs} ms`;
    restartPolling();
  });
}

(async () => {
  await checkAuth();
  bindEvents();
  restartPolling();
  refreshAll();
})();