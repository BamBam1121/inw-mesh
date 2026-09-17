"""Local preview of web/ with the same clean URLs as the laptop's Caddy config:
/install serves install.html, unknown paths get 404.html with a 404 status."""
import http.server, os, sys, urllib.request, urllib.error

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "web")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8099


HELPDESK = "http://127.0.0.1:8090"   # helpdesk/helpdesk.py, if running


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=ROOT, **k)

    def proxy(self):
        """/api/help/* goes to a local help desk, like Caddy does on the laptop."""
        n = int(self.headers.get("Content-Length") or 0)
        req = urllib.request.Request(HELPDESK + self.path, data=self.rfile.read(n) if n else None,
                                     method=self.command, headers={"Content-Type": self.headers.get("Content-Type", "")})
        try:
            r = urllib.request.urlopen(req, timeout=120)
        except urllib.error.HTTPError as e:
            r = e
        except OSError:
            self.send_error(502)
            return
        body = r.read()
        self.send_response(r.status if hasattr(r, "status") else r.code)
        self.send_header("Content-Type", r.headers.get("Content-Type", "application/json"))
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path.startswith("/api/"):
            return self.proxy()
        self.send_error(405)

    def do_GET(self):
        if self.path.startswith("/api/"):
            return self.proxy()
        super().do_GET()

    def send_head(self):
        path = self.path.split("?", 1)[0].split("#", 1)[0]
        full = os.path.join(ROOT, path.lstrip("/"))
        if path != "/" and not os.path.exists(full) and os.path.exists(full + ".html"):
            self.path = path + ".html"
        elif path != "/" and not os.path.exists(full):
            self.send_response(404)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            body = open(os.path.join(ROOT, "404.html"), "rb").read()
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return None
        return super().send_head()


http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
