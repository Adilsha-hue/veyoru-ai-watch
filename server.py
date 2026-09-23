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
import socket
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


def _conversation_input(text, context=""):
    context = str(context or "").strip()[:1600]
    if not context:
        return text
    return "Recent conversation:\n%s\nCurrent user: %s" % (context, text)


def _offline_answer(text):
    prompt = text.lower()
    if "battery" in prompt or "charge" in prompt:
        return "Battery status is shown at the top of the display."
    if "time" in prompt or "date" in prompt:
        return "The live time and date are available on the home screen."
    if "focus" in prompt or "timer" in prompt:
        return "Focus mode is ready. Start with one small task."
    if "hello" in prompt or prompt.startswith("hi"):
        return "Hello. VEYORU offline assistant is ready."
    if "help" in prompt:
        return "Offline I can help with time, battery, focus and simple watch controls."
    return "I am offline. Connect Wi-Fi for a full AI answer."


def _post_json(url, payload, headers=None, timeout=25):
    raw = json.dumps(payload).encode()
    # Cloudflare rejects Python urllib's default User-Agent with error 1010.
    # Identify this gateway explicitly so requests reach the provider API.
    req = Request(url, data=raw, headers={
        "Content-Type": "application/json",
        "Accept": "application/json",
        "User-Agent": "VEYORU/1.0",
        **(headers or {}),
    })
    with urlopen(req, timeout=timeout) as res:
        return json.loads(res.read() or b"{}")


def _groq_answer(text, model, context=""):
    key = os.environ.get("GROQ_API_KEY", "").strip()
    if not key:
        return None
    conversation = _conversation_input(text, context)
    if model.startswith("openai/gpt-oss-"):
        # GPT-OSS spends output tokens on reasoning. Hide that reasoning, use a
        # larger completion budget, and put instructions in the user message as
        # recommended by Groq for these models.
        payload = {
            "model": model,
            "messages": [{"role": "user", "content": SYSTEM_PROMPT + "\n\n" + conversation}],
            "max_completion_tokens": 256,
            "temperature": 0.6,
            "reasoning_effort": "low",
            "include_reasoning": False,
        }
    else:
        payload = {
            "model": model,
            "messages": [
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": conversation},
            ],
            "max_tokens": 80,
            "temperature": 0.6,
        }
    out = _post_json(
        "https://api.groq.com/openai/v1/chat/completions",
        payload,
        {"Authorization": "Bearer " + key},
    )
    try:
        return out["choices"][0]["message"]["content"].strip()
    except (KeyError, IndexError, AttributeError):
        return None


def _gemini_answer(text, model, context=""):
    key = os.environ.get("GEMINI_API_KEY", "").strip()
    if not key:
        return None
    out = _post_json(
        "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s" % (model, key),
        {"system_instruction": {"parts": [{"text": SYSTEM_PROMPT}]},
         "contents": [{"parts": [{"text": _conversation_input(text, context)}]}],
         "generationConfig": {"maxOutputTokens": 80, "temperature": 0.6}},
    )
    try:
        parts = out["candidates"][0]["content"]["parts"]
        return "".join(p.get("text", "") for p in parts).strip() or None
    except (KeyError, IndexError, AttributeError):
        return None


def _ollama_answer(text, model, context=""):
    base = os.environ.get("OLLAMA_URL", "http://localhost:11434").rstrip("/")
    out = _post_json(
        base + "/api/generate",
        {"model": model, "prompt": SYSTEM_PROMPT + "\n" + _conversation_input(text, context),
         "stream": False, "options": {"num_predict": 80, "temperature": 0.6}},
        timeout=60,
    )
    ans = (out.get("response") or "").strip()
    return ans or None


def _openai_answer(text, model, context=""):
    key = os.environ.get("OPENAI_API_KEY", "").strip()
    if not key:
        return None
    # Responses API (same shape as original server.py)
    out = _post_json(
        "https://api.openai.com/v1/responses",
        {"model": model, "input": SYSTEM_PROMPT + " " + _conversation_input(text, context), "max_output_tokens": 80},
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


def _lan_ips():
    """Return private/LAN IPv4 addresses suitable for the watch, never localhost."""
    addresses = set()
    try:
        for item in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            address = item[4][0]
            if not address.startswith(("127.", "169.254.")):
                addresses.add(address)
    except OSError:
        pass
    return sorted(addresses)


def _backend_status(provider):
    """Report configuration separately from verified request-time availability."""
    configured = {
        "groq": bool(os.environ.get("GROQ_API_KEY", "").strip()),
        "gemini": bool(os.environ.get("GEMINI_API_KEY", "").strip()),
        "openai": bool(os.environ.get("OPENAI_API_KEY", "").strip()),
        "local": True,
    }.get(provider, False)
    if provider != "ollama":
        return configured, None
    base = os.environ.get("OLLAMA_URL", "http://localhost:11434").rstrip("/")
    try:
        with urlopen(base + "/api/tags", timeout=2) as response:
            return True, 200 <= response.status < 300
    except (OSError, URLError, TimeoutError):
        return True, False


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
            configured, reachable = _backend_status(provider)
            port = self.server.server_address[1]
            self._json({"ok": True, "provider": provider, "model": default_model(provider),
                        "online_ready": configured and reachable is not False,
                        "backend_configured": configured,
                        "backend_reachable": reachable,
                        "board_assistant_urls": ["http://%s:%d/api/assistant" % (ip, port)
                                                 for ip in _lan_ips()],
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
            context = str(data.get("context", "")).strip()[:1600]
            provider = pick_provider()
            if str(data.get("provider", "")).strip().lower() in ("groq", "gemini", "ollama", "openai", "local"):
                provider = data["provider"].strip().lower()
            model = str(data.get("model", "")).strip() or default_model(provider)
            answer, mode = None, provider
            backend_error = None
            try:
                if provider == "groq":
                    answer = _groq_answer(text, model, context)
                elif provider == "gemini":
                    answer = _gemini_answer(text, model, context)
                elif provider == "ollama":
                    answer = _ollama_answer(text, model, context)
                elif provider == "openai":
                    answer = _openai_answer(text, model, context)
            except HTTPError as exc:
                # HTTPError subclasses URLError, so handle it first or useful
                # provider details (invalid key, model access, quota) are lost.
                try:
                    detail = json.loads(exc.read().decode("utf-8", "replace"))
                    detail = detail.get("error", detail)
                    if isinstance(detail, dict):
                        detail = detail.get("message") or detail.get("code") or str(detail)
                    detail = str(detail)[:240]
                except Exception:
                    detail = str(exc.reason or exc)[:240]
                backend_error = "HTTP %d: %s" % (exc.code, detail)
                answer, mode = _offline_answer(text), "offline"
                sys.stderr.write("assistant backend rejected request: %s\n" % backend_error)
            except (URLError, TimeoutError) as exc:
                # offline / unreachable -> stay usable with local demo reply
                answer, mode = _offline_answer(text), "offline"
                reason = getattr(exc, "reason", exc)
                backend_error = "%s: %s" % (type(reason).__name__, reason)
                sys.stderr.write("assistant backend unavailable: %s\n" % backend_error)
            except json.JSONDecodeError as exc:
                self._json({"answer": "The assistant gateway is unavailable right now.",
                            "mode": "error", "provider": provider, "error": str(exc)}, 502)
                return
            if not answer:
                # no key / unreachable -> offline demo so watch interaction still works
                answer, mode = _offline_answer(text), "offline"
            payload = {"answer": answer[:320], "mode": mode, "provider": provider, "model": model}
            if backend_error:
                payload["backend_error"] = backend_error
            self._json(payload)
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
    host = os.environ.get("HOST", opt("--host", "0.0.0.0"))
    print(f"VEYORU lab: http://{host}:{port}/  provider={pick_provider()} model={default_model(pick_provider())}")
    print("Health: http://%s:%d/api/health" % (host, port))
    ThreadingHTTPServer((host, port), Handler).serve_forever()
