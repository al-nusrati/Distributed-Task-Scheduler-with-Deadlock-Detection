import json
import os
import fcntl
from flask import Flask, jsonify, request, session
from flask_cors import CORS
from werkzeug.utils import secure_filename
from werkzeug.security import generate_password_hash, check_password_hash

app = Flask(__name__)
CORS(app, supports_credentials=True)
app.secret_key = os.environ.get('SECRET_KEY', 'dev-secret-change-in-production')

BASE       = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "shared"))
INPUT_FILE = os.path.join(BASE, "input.json")
STATE_FILE = os.path.join(BASE, "state.json")
MODE_FILE  = os.path.join(BASE, "scheduler_mode.txt")
LOCK_FILE  = os.path.join(BASE, "input.lock")
UPLOAD_DIR = os.path.join(BASE, "uploads")
USERS_FILE = os.path.join(os.path.dirname(__file__), "users.json")

ALLOWED_EXTENSIONS = {'py', 'cpp', 'c', 'js', 'java'}

os.makedirs(UPLOAD_DIR, exist_ok=True)


# ---------- User Database Helpers ----------
def load_users():
    if not os.path.exists(USERS_FILE):
        return {}
    with open(USERS_FILE, 'r', encoding='utf-8') as f:
        return json.load(f)

def save_users(users):
    with open(USERS_FILE, 'w', encoding='utf-8') as f:
        json.dump(users, f, indent=2)


# ---------- File Locking ----------
def _ensure_lock_file():
    if not os.path.exists(LOCK_FILE):
        open(LOCK_FILE, "w").close()

def _read_json_unlocked(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            content = f.read().strip()
            return json.loads(content) if content else {}
    except Exception:
        return {}

def _write_json_unlocked(path, data):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)

def read_json(path):
    return _read_json_unlocked(path)

def ensure_input_structure(data):
    data.setdefault("pending_tasks", [])
    data.setdefault("reset_flag", False)
    data.setdefault("processing_enabled", False)
    return data

class _InputLock:
    def __enter__(self):
        _ensure_lock_file()
        self._lf = open(LOCK_FILE, "r")
        fcntl.flock(self._lf, fcntl.LOCK_EX)
        return self

    def __exit__(self, *_):
        fcntl.flock(self._lf, fcntl.LOCK_UN)
        self._lf.close()


# ---------- Authentication Routes ----------
@app.route("/api/register", methods=["POST"])
def register():
    data = request.get_json()
    username = data.get("username", "").strip()
    password = data.get("password", "")

    if not username or not password:
        return jsonify({"error": "Username and password required"}), 400

    users = load_users()
    if username in users:
        return jsonify({"error": "Username already taken"}), 400

    users[username] = {
        "password_hash": generate_password_hash(password)
    }
    save_users(users)
    session["user"] = username
    return jsonify({"message": "Account created", "user": username}), 201


@app.route("/api/login", methods=["POST"])
def login():
    data = request.get_json()
    username = data.get("username", "").strip()
    password = data.get("password", "")

    users = load_users()
    user = users.get(username)
    if not user or not check_password_hash(user["password_hash"], password):
        return jsonify({"error": "Invalid username or password"}), 401

    session["user"] = username
    return jsonify({"message": "Logged in", "user": username}), 200


@app.route("/api/logout", methods=["POST"])
def logout():
    session.pop("user", None)
    return jsonify({"message": "Logged out"}), 200


@app.route("/api/current_user", methods=["GET"])
def current_user():
    return jsonify({"user": session.get("user")})


# ---------- Protected Task Routes ----------
def _require_login():
    if "user" not in session:
        return jsonify({"error": "Unauthorized"}), 401
    return None

@app.route("/api/upload", methods=["POST"])
def upload_file():
    auth_error = _require_login()
    if auth_error:
        return auth_error

    if 'file' not in request.files:
        return jsonify({"error": "No file part"}), 400

    file = request.files['file']
    if file.filename == '':
        return jsonify({"error": "No selected file"}), 400

    ext = file.filename.rsplit('.', 1)[-1].lower() if '.' in file.filename else ''
    if ext not in ALLOWED_EXTENSIONS:
        return jsonify({"error": f"File type .{ext} not allowed. Use: {', '.join(ALLOWED_EXTENSIONS)}"}), 400

    scheduling_mode = request.form.get("scheduling_mode", "priority")
    if scheduling_mode not in ("priority", "round_robin"):
        scheduling_mode = "priority"

    language_map = {"py": "python", "cpp": "cpp", "c": "c", "js": "js", "java": "java"}
    language = language_map.get(ext, "unknown")

    try:
        content = file.read().decode('utf-8')
    except Exception as e:
        return jsonify({"error": f"Could not read file: {str(e)}"}), 400

    # Save a copy for debugging
    safe_name = secure_filename(file.filename)
    save_path = os.path.join(UPLOAD_DIR, safe_name)
    with open(save_path, 'w', encoding='utf-8') as f:
        f.write(content)

    new_task = {
        "filename": file.filename,
        "language": language,
        "submitted_by": session["user"],
        "file_content": content,
        "scheduling_mode": scheduling_mode
    }

    with _InputLock():
        data = ensure_input_structure(_read_json_unlocked(INPUT_FILE))
        data["pending_tasks"].append(new_task)
        _write_json_unlocked(INPUT_FILE, data)
        pending_count = len(data["pending_tasks"])
        processing_enabled = bool(data["processing_enabled"])

    return jsonify({
        "message": "File uploaded and task queued",
        "pending_count": pending_count,
        "processing_enabled": processing_enabled,
    }), 201


# ---------- Other Routes (unchanged except optional login checks) ----------
@app.route("/api/status", methods=["GET"])
def get_status():
    return jsonify(read_json(STATE_FILE))


@app.route("/api/input/status", methods=["GET"])
def get_input_status():
    with _InputLock():
        data = ensure_input_structure(_read_json_unlocked(INPUT_FILE))
    return jsonify({
        "pending_count": len(data["pending_tasks"]),
        "processing_enabled": bool(data["processing_enabled"])
    })


@app.route("/api/input/start", methods=["POST"])
def start_processing():
    with _InputLock():
        data = ensure_input_structure(_read_json_unlocked(INPUT_FILE))
        if not data["pending_tasks"]:
            data["processing_enabled"] = False
            _write_json_unlocked(INPUT_FILE, data)
            return jsonify({"error": "No pending tasks to start"}), 400

        data["processing_enabled"] = True
        _write_json_unlocked(INPUT_FILE, data)
        pending_count = len(data["pending_tasks"])

    return jsonify({
        "message": "Scheduler input processing started",
        "pending_count": pending_count,
        "processing_enabled": True,
    }), 200


@app.route("/api/scheduler/mode", methods=["POST"])
def set_mode():
    mode = request.get_json().get("mode", "priority")
    if mode not in ("priority", "round_robin"):
        return jsonify({"error": "Invalid mode"}), 400
    with open(MODE_FILE, "w", encoding="utf-8") as f:
        f.write(mode)
    return jsonify({"message": f"Mode set to {mode}"}), 200


@app.route("/api/scheduler/mode", methods=["GET"])
def get_mode():
    try:
        with open(MODE_FILE, "r", encoding="utf-8") as f:
            mode = f.read().strip()
    except Exception:
        mode = "priority"
    return jsonify({"mode": mode})


if __name__ == "__main__":
    _ensure_lock_file()
    app.run(host="0.0.0.0", port=5000, debug=True)