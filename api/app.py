import json
import os
from flask import Flask, jsonify, request
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

BASE = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "shared"))
INPUT_FILE = os.path.join(BASE, "input.json")
STATE_FILE = os.path.join(BASE, "state.json")
MODE_FILE = os.path.join(BASE, "scheduler_mode.txt")


def read_json(path):
    try:
        with open(path, "r", encoding="utf-8") as file:
            content = file.read().strip()
            return json.loads(content) if content else {}
    except Exception:
        return {}


def write_json(path, data):
    with open(path, "w", encoding="utf-8") as file:
        json.dump(data, file, indent=2)


def ensure_input_structure(data):
    data.setdefault("pending_tasks", [])
    data.setdefault("reset_flag", False)
    data.setdefault("processing_enabled", False)
    return data


@app.route("/api/status", methods=["GET"])
def get_status():
    return jsonify(read_json(STATE_FILE))


@app.route("/api/input/status", methods=["GET"])
def get_input_status():
    data = ensure_input_structure(read_json(INPUT_FILE))
    return jsonify({
        "pending_count": len(data["pending_tasks"]),
        "processing_enabled": bool(data["processing_enabled"])
    })


@app.route("/api/task/add", methods=["POST"])
def add_task():
    body = request.get_json()

    if not body or "file_path" not in body:
        return jsonify({"error": "Missing file_path"}), 400

    file_path = body["file_path"]
    submitted_by = body.get("submitted_by", "Unknown")

    if not os.path.exists(file_path):
        return jsonify({"error": "File not found"}), 400

    ext = file_path.split(".")[-1]
    language_map = {
        "py": "python",
        "cpp": "cpp",
        "c": "c",
        "js": "js",
        "java": "java"
    }
    language = language_map.get(ext, "unknown")

    with open(file_path, "r", encoding="utf-8") as file:
        content = file.read()

    new_task = {
        "filename": os.path.basename(file_path),
        "language": language,
        "submitted_by": submitted_by,
        "file_content": content
    }

    data = ensure_input_structure(read_json(INPUT_FILE))
    data["pending_tasks"].append(new_task)
    write_json(INPUT_FILE, data)

    return jsonify({
        "message": "Task queued in input.json",
        "pending_count": len(data["pending_tasks"]),
        "processing_enabled": bool(data["processing_enabled"])
    }), 201


@app.route("/api/input/start", methods=["POST"])
def start_processing():
    data = ensure_input_structure(read_json(INPUT_FILE))

    if not data["pending_tasks"]:
        data["processing_enabled"] = False
        write_json(INPUT_FILE, data)
        return jsonify({"error": "No pending tasks to start"}), 400

    data["processing_enabled"] = True
    write_json(INPUT_FILE, data)

    return jsonify({
        "message": "Scheduler input processing started",
        "pending_count": len(data["pending_tasks"]),
        "processing_enabled": True
    }), 200


@app.route("/api/scheduler/mode", methods=["POST"])
def set_mode():
    mode = request.get_json().get("mode", "priority")

    if mode not in ("priority", "round_robin"):
        return jsonify({"error": "Invalid mode"}), 400

    with open(MODE_FILE, "w", encoding="utf-8") as file:
        file.write(mode)

    return jsonify({"message": f"Mode set to {mode}"}), 200


@app.route("/api/scheduler/mode", methods=["GET"])
def get_mode():
    try:
        with open(MODE_FILE, "r", encoding="utf-8") as file:
            mode = file.read().strip()
    except Exception:
        mode = "priority"

    return jsonify({"mode": mode})


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)

