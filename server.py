"""VEYORU lab gateway - serves index.html + free LLM backends (stdlib only).

Providers (env LLM_PROVIDER=auto|groq|gemini|ollama|openai|local):
  groq   - GROQ_API_KEY, LLM_MODEL or GROQ_MODEL (default llama-3.1-8b-instant) FREE
  gemini - GEMINI_API_KEY, LLM_MODEL or GEMINI_MODEL (default gemini-2.5-flash-lite) FREE
  ollama - OLLAMA_URL (default http://localhost:11434), LLM_MODEL or OLLAMA_MODEL (default llama3.2:3b) FREE/local
  openai - OPENAI_API_KEY, LLM_MODEL or OPENAI_MODEL (default gpt-5-mini) PAID
  local  - always offline demo reply, no key needed
  auto   - first available key: GROQ > GEMINI > OPENAI, else try ollama, else local-demo

Windows:
  py -3 server.py --port 8000
  set GROQ_API_KEY=gsk_... & py -3 server.py --port 8000 --host 0.0.0.0
Hotspot/LAN: use --host 0.0.0.0 so phone/watch on same hotspot can reach
  http://<laptop-ip>:8000/api/assistant
"""
import json
import os
import sys
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError

ROOT = os.path.dirname(os.path.abspath(__file__))
SYSTEM_PROMPT = (
    "You are VEYORU, a concise smartwatch assistant. "
    "Reply in 1-2 short lines, max 160 characters total. "
    "Plain text only, no markdown, no emoji, no bullet lists."
)
LOCAL_DEMO = (
    "I can help with that. Start by defining one small next step, "
    "then tap the watch again when you are ready."
)


def _post_json(url, payload, headers=None, timeout=25):
    raw = json.dumps(payload).encode()
    req = Request(url, data=raw, headers={"Content-Type": "application/json", **(headers or {})})
    with urlopen(req, timeout=timeout) as res:
        return json.loads(res.read() or b"{}")


def _groq_answer(text, model):
    key = os.environ.get("GROQ_API_KEY", "").strip()
    if not key:
        return None
    out = _post_json(
        "https://api.groq.com/openai/v1/chat/completions",
        {"model": model, "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": text}],
         "max_tokens": 80, "temperature": 0.6},
        {"Authorization": "Bearer " + key},
    )
    try:
        return out["choices"][0]["message"]["content"].strip()
    except (KeyError, IndexError, AttributeError):
        return None


def _gemini_answer(text, model):
    key = os.environ.get("GEMINI_API_KEY", "").strip()
    if not key:
        return None
    out = _post_json(
        "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s" % (model, key),
        {"system_instruction": {"parts": [{"text": SYSTEM_PROMPT}]},
         "contents": [{"parts": [{"text": text}]}],
         "generationConfig": {"maxOutputTokens": 80, "temperature": 0.6}},
    )
    try:
        parts = out["candidates"][0]["content"]["parts"]
        return "".join(p.get("text", "") for p in parts).strip() or None
    except (KeyError, IndexError, AttributeError):
        return None


def _ollama_answer(text, model):
    base = os.environ.get("OLLAMA_URL", "http://localhost:11434").rstrip("/")
    out = _post_json(
        base + "/api/generate",
        {"model": model, "prompt": SYSTEM_PROMPT + "\nUser: " + text,
         "stream": False, "options": {"num_predict": 80, "temperature": 0.6}},
        timeout=60,
    )
    ans = (out.get("response") or "").strip()
    return ans or None


def _openai_answer(text, model):
    key = os.environ.get("OPENAI_API_KEY", "").strip()
    if not key:
        return None
    # Responses API (same shape as original server.py)
    out = _post_json(
        "https://api.openai.com/v1/responses",
        {"model": model, "input": SYSTEM_PROMPT + " User: " + text, "max_output_tokens": 80},
        {"Authorization": "Bearer " + key},
    )
    answer = out.get("output_text", "")
    if not answer:
        for item in out.get("output", []):
            for c in item.get("content", []):
                t = c.get("text")
                if isinstance(t, dict):
                    answer = t.get("value", "")
                elif isinstance(t, str):
                    answer = t
                if answer:
                    break
    return (answer or "").strip() or None


def pick_provider():
    want = os.environ.get("LLM_PROVIDER", "auto").strip().lower() or "auto"
    if want != "auto":
        return want
    if os.environ.get("GROQ_API_KEY", "").strip():
        return "groq"
    if os.environ.get("GEMINI_API_KEY", "").strip():
        return "gemini"
    if os.environ.get("OPENAI_API_KEY", "").strip():
        return "openai"
    return "ollama"  # probed at request time, falls back to local-demo


def default_model(provider):
    return {
        "groq": os.environ.get("LLM_MODEL") or os.environ.get("GROQ_MODEL") or "llama-3.1-8b-instant",
        "gemini": os.environ.get("LLM_MODEL") or os.environ.get("GEMINI_MODEL") or "gemini-2.5-flash-lite",
        "ollama": os.environ.get("LLM_MODEL") or os.environ.get("OLLAMA_MODEL") or "llama3.2:3b",
        "openai": os.environ.get("LLM_MODEL") or os.environ.get("OPENAI_MODEL") or "gpt-5-mini",
    }.get(provider, "local-demo")


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def log_message(self, fmt, *args):
        if self.path.startswith("/api/"):
            sys.stderr.write("api %s %s\n" % (self.command, self.path))
            return
        super().log_message(fmt, *args)

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        if self.path == "/api/health":
            provider = pick_provider()
            self._json({"ok": True, "provider": provider, "model": default_model(provider),
                        "has_groq": bool(os.environ.get("GROQ_API_KEY")),
                        "has_gemini": bool(os.environ.get("GEMINI_API_KEY")),
                        "has_openai": bool(os.environ.get("OPENAI_API_KEY"))})
            return
        return super().do_GET()

    def do_POST(self):
        if self.path != "/api/assistant":
            self.send_error(404)
            return
        try:
            n = int(self.headers.get("Content-Length", "0"))
            data = json.loads(self.rfile.read(n) or b"{}")
            text = str(data.get("text", "")).strip()[:1000] or "What should we build today?"
            provider = pick_provider()
            if str(data.get("provider", "")).strip().lower() in ("groq", "gemini", "ollama", "openai", "local"):
                provider = data["provider"].strip().lower()
            model = str(data.get("model", "")).strip() or default_model(provider)
            answer, mode = None, provider
            try:
                if provider == "groq":
                    answer = _groq_answer(text, model)
                elif provider == "gemini":
                    answer = _gemini_answer(text, model)
                elif provider == "ollama":
                    answer = _ollama_answer(text, model)
                elif provider == "openai":
                    answer = _openai_answer(text, model)
            except (URLError, TimeoutError) as exc:
                # offline / unreachable -> stay usable with local demo reply
                answer, mode = LOCAL_DEMO, "local-demo"
            except (HTTPError, json.JSONDecodeError) as exc:
                self._json({"answer": "The assistant gateway is unavailable right now.",
                            "mode": "error", "provider": provider, "error": str(exc)}, 502)
                return
            if not answer:
                # no key / unreachable -> offline demo so watch interaction still works
                answer, mode = LOCAL_DEMO, "local-demo"
            self._json({"answer": answer[:320], "mode": mode, "provider": provider, "model": model})
        except Exception as exc:  # never break the watch UI
            self._json({"answer": "The assistant gateway is unavailable right now.",
                        "mode": "error", "error": str(exc)}, 502)

    def _json(self, payload, status=200):
        raw = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


if __name__ == "__main__":
    args = sys.argv[1:]
    def opt(name, default):
        return args[args.index(name) + 1] if name in args and args.index(name) + 1 < len(args) else default
    port = int(os.environ.get("PORT", opt("--port", "8000")))
    host = os.environ.get("HOST", opt("--host", "127.0.0.1"))
    print(f"VEYORU lab: http://{host}:{port}/  provider={pick_provider()} model={default_model(pick_provider())}")
    print("Health: http://%s:%d/api/health" % (host, port))
    ThreadingHTTPServer((host, port), Handler).serve_forever()
