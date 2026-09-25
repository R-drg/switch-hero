#!/usr/bin/env python3
"""Serves the browser build locally with the headers its threads need.

    python3 tools/serve-web.py [build-web] [port]

then open http://localhost:8000/switch-hero.html
"""
import functools
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()


Handler.extensions_map[".wasm"] = "application/wasm"
root = sys.argv[1] if len(sys.argv) > 1 else "build-web"
port = int(sys.argv[2]) if len(sys.argv) > 2 else 8000
print(f"Serving {root} on http://localhost:{port}/switch-hero.html")
http.server.ThreadingHTTPServer(("", port), functools.partial(Handler, directory=root)).serve_forever()
