import time, random, threading
from collections import OrderedDict
from urllib.parse import urlparse, parse_qs
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
class FastServer(ThreadingHTTPServer):
    request_queue_size = 128
    allow_reuse_address = True
def make_handler(port):
    cache = OrderedDict()
    lock = threading.Lock()
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path == "/reset":
                with lock: cache.clear()
                self.send_response(200); self.end_headers(); self.wfile.write(b"OK")
                return
            q = parse_qs(parsed.query).get("q", ["default"])[0]
            if random.random() < 0.06:
                time.sleep(0.240) # 6% tail-latency stall (240ms)
            with lock:
                hit = q in cache
                if hit: cache.move_to_end(q)
                else:
                    cache[q] = True
                    if len(cache) > 35: cache.popitem(last=False)
            if not hit:
                time.sleep(0.035) # Disk miss (~45ms on Windows clock); RAM hit returns instantly!
            self.send_response(200)
            self.send_header("X-Cache", "HIT" if hit else "MISS")
            self.end_headers()
            self.wfile.write(b"OK")
        def log_message(self, *args): pass
    return Handler
if __name__ == "__main__":
    for p in [8081, 8082, 8083]:
        threading.Thread(target=FastServer(("127.0.0.1", p), make_handler(p)).serve_forever, daemon=True).start()
    while True: time.sleep(1)
