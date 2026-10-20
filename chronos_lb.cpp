#define _WIN32_WINNT 0x0A00
#include "httplib.h"
#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <chrono>
#include <algorithm>
#include <sstream>
using namespace std::chrono;
struct BackendNode {
    std::string host; int port;
    std::atomic<int> active{0};
    std::atomic<double> ewma_ms{10.0};
    BackendNode(std::string h, int p) : host(h), port(p) {}
    void record(double ms) { ewma_ms.store(0.2 * ms + 0.8 * ewma_ms.load()); }
    double score() const { return ewma_ms.load() * (active.load() + 1); }
};
struct ProxyResult { bool ok = false; std::string cache = "MISS"; };
struct RaceState {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false, hedged_won = false;
    ProxyResult winner;
};
ProxyResult call_shard(BackendNode* node, std::string query) {
    node->active++;
    auto start = steady_clock::now();
    httplib::Client cli(node->host, node->port);
    cli.set_connection_timeout(0, 400000);
    cli.set_read_timeout(1, 0);
    auto res = cli.Get(("/search?q=" + query).c_str());
    double ms = duration<double, std::milli>(steady_clock::now() - start).count();
    node->active--;
    if (res && res->status == 200) {
        node->record(ms);
        return {true, res->get_header_value("X-Cache")};
    }
    node->record(250.0);
    return {false, "MISS"};
}
int main() {
    std::vector<BackendNode*> nodes = {
        new BackendNode("127.0.0.1", 8081),
        new BackendNode("127.0.0.1", 8082),
        new BackendNode("127.0.0.1", 8083)
    };
    std::atomic<long> total{0}, hits{0}, h_trig{0}, h_save{0};
    std::atomic<size_t> rr{0};
    std::mutex lat_mtx;
    std::vector<double> lats;
    httplib::Server svr;
    svr.new_task_queue = [] { return new httplib::ThreadPool(24); };
    svr.Get("/search", [&](const httplib::Request& req, httplib::Response& res) {
        std::string q = req.has_param("q") ? req.get_param_value("q") : "x";
        std::string mode = req.has_param("mode") ? req.get_param_value("mode") : "chronos";
        auto t0 = steady_clock::now();
        ProxyResult out;
        if (mode == "rr") {
            out = call_shard(nodes[(rr++) % 3], q);
        } else {
            int p_idx = std::hash<std::string>{}(q) % 3;
            int c1 = (p_idx + 1) % 3, c2 = (p_idx + 2) % 3;
            int b_idx = (nodes[c1]->score() <= nodes[c2]->score()) ? c1 : c2;
            if (nodes[p_idx]->active.load() > 10) p_idx = b_idx;
            BackendNode* p_node = nodes[p_idx];
            BackendNode* b_node = nodes[b_idx];
            auto race = std::make_shared<RaceState>();
            std::thread([race, p_node, q]() {
                ProxyResult r = call_shard(p_node, q);
                std::lock_guard<std::mutex> lk(race->mtx);
                if (!race->done) { race->done = true; race->winner = r; race->cv.notify_one(); }
            }).detach();
            std::unique_lock<std::mutex> lk(race->mtx);
            if (!race->cv.wait_for(lk, milliseconds(85), [&]{ return race->done; })) {
                h_trig++;
                std::thread([race, b_node, q]() {
                    ProxyResult r = call_shard(b_node, q);
                    std::lock_guard<std::mutex> hlk(race->mtx);
                    if (!race->done) {
                        race->done = true; race->hedged_won = true;
                        race->winner = r; race->cv.notify_one();
                    }
                }).detach();
                race->cv.wait(lk, [&]{ return race->done; });
            }
            out = race->winner;
            if (race->hedged_won) h_save++;
        }
        double ms = duration<double, std::milli>(steady_clock::now() - t0).count();
        total++;
        if (out.cache == "HIT") hits++;
        { std::lock_guard<std::mutex> lk(lat_mtx); lats.push_back(ms); }
        res.set_header("X-Cache", out.cache);
        res.set_content("OK", "text/plain");
    });
    svr.Get("/metrics", [&](const httplib::Request& req, httplib::Response& res) {
        if (req.has_param("reset")) {
            total = 0; hits = 0; h_trig = 0; h_save = 0;
            std::lock_guard<std::mutex> lk(lat_mtx); lats.clear();
            res.set_content("{}", "application/json"); return;
        }
        std::vector<double> s;
        { std::lock_guard<std::mutex> lk(lat_mtx); s = lats; }
        double p50 = 0, p95 = 0, p99 = 0;
        if (!s.empty()) {
            std::sort(s.begin(), s.end());
            p50 = s[s.size() * 0.50]; p95 = s[s.size() * 0.95]; p99 = s[s.size() * 0.99];
        }
        double hr = (100.0 * hits.load()) / std::max(1L, total.load());
        std::ostringstream o;
        o << "{\"cache_hit_rate_pct\":" << hr << ",\"p50_latency_ms\":" << p50
          << ",\"p95_latency_ms\":" << p95 << ",\"p99_latency_ms\":" << p99
          << ",\"hedged_tail_saves\":" << h_save.load() << "}";
        res.set_content(o.str(), "application/json");
    });
    std::cout << "[*] Attempting to bind ChronosLB to http://127.0.0.1:8080...\n"; if (!svr.bind("127.0.0.1", 8080)) { std::cerr << "[!] ERROR: Failed to bind port 8080 (Port already in use or permission denied)!\n"; return 1; } std::cout << "[+] ChronosLB successfully bound and listening!\n"; svr.listen_after_bind();
}


