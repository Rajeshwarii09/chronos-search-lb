import urllib.request
import json
from concurrent.futures import ThreadPoolExecutor
SEARCH_QUERIES = [f"search_term_{i}" for i in range(40)]
def send_query(args):
    q, mode = args
    url = f"http://127.0.0.1:8080/search?q={q}&mode={mode}"
    try:
        with urllib.request.urlopen(url, timeout=3.0) as resp:
            resp.read()
    except Exception:
        pass
def run_suite(mode, total_requests=200):
    for port in [8081, 8082, 8083]:
        try:
            urllib.request.urlopen(f"http://127.0.0.1:{port}/reset", timeout=2.0).read()
        except:
            pass
    try:
        urllib.request.urlopen("http://127.0.0.1:8080/metrics?reset=1", timeout=2.0).read()
    except:
        pass
    tasks = [(SEARCH_QUERIES[i % len(SEARCH_QUERIES)], mode) for i in range(total_requests)]
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(send_query, tasks))
    with urllib.request.urlopen("http://127.0.0.1:8080/metrics", timeout=3.0) as resp:
        return json.loads(resp.read().decode())
if __name__ == "__main__":
    print("1. Running Baseline (Naive Round-Robin)...")
    rr = run_suite("rr")
    print("2. Running ChronosLB (Bounded Consistent Hashing + EWMA P2C + Hedging)...")
    ch = run_suite("chronos")
    print("\n================ BENCHMARK RESULTS ================")
    print(f"{'Metric':<28} | {'Round-Robin':<14} | {'ChronosLB':<14}")
    print("-" * 58)
    print(f"{'Cache Hit Rate (%)':<28} | {rr['cache_hit_rate_pct']:>12.1f}% | {ch['cache_hit_rate_pct']:>12.1f}%")
    print(f"{'Median Latency (p50)':<28} | {rr['p50_latency_ms']:>11.2f} ms | {ch['p50_latency_ms']:>11.2f} ms")
    print(f"{'Tail Latency (p95)':<28} | {rr['p95_latency_ms']:>11.2f} ms | {ch['p95_latency_ms']:>11.2f} ms")
    print(f"{'Extreme Tail Latency (p99)':<28} | {rr['p99_latency_ms']:>11.2f} ms | {ch['p99_latency_ms']:>11.2f} ms")
    print(f"{'Hedged Tail Saves':<28} | {rr['hedged_tail_saves']:>14} | {ch['hedged_tail_saves']:>14}")
    print("===================================================")
