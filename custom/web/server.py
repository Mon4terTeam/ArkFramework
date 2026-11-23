from __future__ import annotations

import argparse
import json
import mimetypes
import threading
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse

from custom.parser.coverage import build_coverage, write_reports
from custom.parser.loader import load_store
from custom.parser.resolver import Resolver


CUSTOM_ROOT = Path(__file__).resolve().parents[1]
STATIC_ROOT = Path(__file__).resolve().parent / "static"


class ViewerData:
    def __init__(self, root: Path | None = None) -> None:
        self.root = root or CUSTOM_ROOT.parents[1]
        self.store = load_store(root=self.root)
        self.resolver = Resolver(self.store)
        self._lock = threading.Lock()
        self._coverage: dict[str, Any] | None = None
        self._operator_cache: dict[str, dict[str, Any]] = {}
        self._ensure_coverage()

    def _ensure_coverage(self) -> None:
        out = CUSTOM_ROOT / "out"
        coverage_path = out / "coverage.json"
        if coverage_path.exists():
            self._coverage = json.loads(coverage_path.read_text(encoding="utf-8"))
            return
        with self._lock:
            operators = self.resolver.resolve_all_operators()
            report = build_coverage(self.store, self.resolver, operators)
            write_reports(out, report, self.resolver.unresolved)
            self._coverage = json.loads(coverage_path.read_text(encoding="utf-8"))

    def coverage(self) -> dict[str, Any]:
        self._ensure_coverage()
        assert self._coverage is not None
        return self._coverage

    def operator(self, operator_id: str) -> dict[str, Any]:
        cached = self._operator_cache.get(operator_id)
        if cached is not None:
            return cached
        operator = self.store.operator_by_id.get(operator_id)
        if operator is None:
            raise KeyError(operator_id)
        with self._lock:
            report = self.resolver.resolve_operator(operator, full=True)
        self._operator_cache[operator_id] = report
        return report

    def buff(self, template_key: str) -> dict[str, Any]:
        with self._lock:
            return self.resolver.resolve_buff(template_key, full=True)


class Handler(BaseHTTPRequestHandler):
    data: ViewerData

    def log_message(self, fmt: str, *args: Any) -> None:
        # Keep the console useful but quiet enough for a local viewer.
        if args and str(args[0]).startswith("4"):
            super().log_message(fmt, *args)

    def _json(self, value: Any, status: int = HTTPStatus.OK) -> None:
        payload = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def _static(self, path: Path) -> None:
        if not path.is_file():
            self.send_error(HTTPStatus.NOT_FOUND)
            return
        payload = path.read_bytes()
        content_type, _ = mimetypes.guess_type(str(path))
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type or "application/octet-stream")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)
        try:
            if path == "/api/coverage":
                self._json(self.data.coverage())
                return
            if path == "/api/operators":
                self._json({"operators": self.data.coverage().get("operators", [])})
                return
            if path == "/api/operator":
                operator_id = (query.get("id") or [""])[0]
                if not operator_id:
                    self._json({"error": "missing id"}, HTTPStatus.BAD_REQUEST)
                    return
                self._json(self.data.operator(operator_id))
                return
            if path == "/api/runtime":
                self._json(
                    self.data.coverage().get("needsRuntimeVerification", {})
                )
                return
            if path == "/api/buff":
                key = (query.get("key") or [""])[0]
                if not key:
                    self._json({"error": "missing key"}, HTTPStatus.BAD_REQUEST)
                    return
                self._json(self.data.buff(key))
                return
            if path in ("/", "/index.html"):
                self._static(STATIC_ROOT / "index.html")
                return
            relative = path.lstrip("/")
            candidate = (STATIC_ROOT / relative).resolve()
            if STATIC_ROOT not in candidate.parents and candidate != STATIC_ROOT:
                self.send_error(HTTPStatus.FORBIDDEN)
                return
            self._static(candidate)
        except KeyError:
            self._json({"error": "not found"}, HTTPStatus.NOT_FOUND)
        except Exception as exc:  # keep the viewer alive on parser errors
            self._json({"error": str(exc)}, HTTPStatus.INTERNAL_SERVER_ERROR)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="custom.web",
        description="Local web viewer for the custom-skill parser.",
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--root", default=None, help="ArkTest root")
    parser.add_argument("--open", action="store_true", help="open the browser")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    data = ViewerData(Path(args.root).resolve() if args.root else None)
    handler = type("BoundHandler", (Handler,), {"data": data})
    server = HTTPServer((args.host, args.port), handler)
    url = f"http://{args.host}:{args.port}/"
    print(f"custom skill viewer: {url}")
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        server.server_close()
    return 0

