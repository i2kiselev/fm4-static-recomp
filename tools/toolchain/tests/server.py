"""A local HTTP server for the download tests."""
import contextlib
import re
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

_RANGE = re.compile(r"bytes=(\d+)-(\d*)$")


@contextlib.contextmanager
def serve(files, honour_range=True, fail=frozenset(), cut_once=frozenset(), busy_once=frozenset()):
    """Serves `files` (path -> bytes) on 127.0.0.1; yields the base URL. `hits` counts requests per path.

    A ranged request for a path in `cut_once` that starts at 0 (not the 1-byte probe) gets half its bytes and a closed
    connection the first time, as a player's line drops one connection of several. The first request for a path in
    `busy_once` gets a 503.
    """
    cut = set()
    busy = set()

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            path = self.path.split("?", 1)[0]
            with lock:
                server.hits[path] = server.hits.get(path, 0) + 1
                answer_busy = path in busy_once and path not in busy
                busy.add(path)
            if answer_busy:
                self.send_error(503)
                return
            if path in fail:
                self.send_error(500)
                return
            body = files.get(path)
            if body is None:
                self.send_error(404)
                return
            match = _RANGE.match(self.headers.get("Range", ""))
            if honour_range and match:
                start = int(match.group(1))
                end = int(match.group(2)) if match.group(2) else len(body) - 1
                part = body[start:end + 1]
                self.send_response(206)
                self.send_header("Content-Range", f"bytes {start}-{end}/{len(body)}")
                self.send_header("Content-Length", str(len(part)))
                self.end_headers()
                with lock:
                    cutting = path in cut_once and start == 0 and end > 0 and path not in cut
                    if cutting:
                        cut.add(path)
                if cutting:
                    self.wfile.write(part[:len(part) // 2])
                    self.close_connection = True
                    return
                self.wfile.write(part)
                return
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    lock = threading.Lock()
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.hits = {}
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    serve.servers[base] = server
    try:
        yield base
    finally:
        del serve.servers[base]
        server.shutdown()
        server.server_close()


serve.servers = {}


def hits(base, suffix):
    """Requests the server at `base` got for paths ending in `suffix`."""
    return sum(n for path, n in serve.servers[base].hits.items() if path.endswith(suffix))
