"""Local preview of web/ with the same clean URLs as the laptop's Caddy config:
/install serves install.html, unknown paths get 404.html with a 404 status."""
import http.server, os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "web")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8099


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=ROOT, **k)

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
