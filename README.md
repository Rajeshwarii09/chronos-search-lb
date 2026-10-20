# ChronosLB: Search-Aware L7 Reverse Proxy & Tail-Latency Router
A high-performance, multithreaded Layer-7 HTTP reverse proxy built from scratch in **C++20**, designed specifically for Information Retrieval (Search) workloads. It implements Google's *"Tail at Scale"* tail-latency mitigation principles alongside distributed caching patterns.
---
## 🚀 Key Architectural Features
1. **Consistent Hashing & Cache Locality:** Maps incoming search queries deterministically to specific backend shards to eliminate cache thrashing and maximize in-memory LRU RAM cache hit rates.
2. **EWMA Power-of-Two-Choices (P2C) Load Balancing:** Dynamically routes traffic by combining active connection counts with Exponentially Weighted Moving Average (EWMA) latency telemetry.
3. **Non-Blocking Request Hedging:** Inspired by Google's tail-latency research, it uses detached C++ threads (`.detach()`) and `std::condition_variable` synchronization to race backup nodes if a primary shard exceeds the $p_{95}$ latency threshold.
4. **Fault Tolerance:** Features state-machine **Circuit Breakers** (tripping on consecutive high-latency failures) and **Token Bucket** rate limiting to prevent cascading failures.
---
## 🛠️ Tech Stack
* **Language:** C++20 (STL, Atomics, Multithreading, Condition Variables)
* **Networking:** HTTP/S sockets via `httplib.h` & WinSock (`-lws2_32`)
* **Testing & Harness:** Python, Concurrent Futures (`ThreadPoolExecutor`), Custom Mock Shards with LRU Caching and Chaos Injection.
---
## 📂 Project Structure
```text
chronos-search-lb/
├── chronos_lb.cpp        # C++20 L7 Reverse Proxy core logic
├── search_shard.py       # Simulated search backends with RAM cache & tail-latency stalls
├── benchmark.py          # Concurrent multi-threaded load testing suite
└── httplib.h             # Single-header HTTP library
