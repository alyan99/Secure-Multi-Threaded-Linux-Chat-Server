"""Measured local request latency/throughput; outputs actual results as JSON."""
import concurrent.futures
import json
import statistics
import time
from support import RunningServer


def main():
    server = RunningServer()
    peers = []
    try:
        peers = [server.peer() for _ in range(20)]
        def run(peer):
            latencies = []
            for _ in range(10):
                started = time.perf_counter()
                response = peer.request("/help")
                if not response.startswith("OK commands"):
                    raise AssertionError(response)
                latencies.append((time.perf_counter() - started) * 1000)
            return latencies
        started = time.perf_counter()
        with concurrent.futures.ThreadPoolExecutor(max_workers=20) as executor:
            results = list(executor.map(run, peers))
        elapsed = time.perf_counter() - started
        latencies = sorted(value for result in results for value in result)
        print(json.dumps({"clients": 20, "requests": len(latencies),
                          "workload": "unauthenticated /help, excludes password hashing",
                          "elapsed_seconds": elapsed,
                          "requests_per_second": len(latencies) / elapsed,
                          "mean_latency_ms": statistics.mean(latencies),
                          "p95_latency_ms": latencies[int(0.95 * (len(latencies) - 1))]}, indent=2))
    finally:
        for peer in peers:
            peer.close()
        server.close()

if __name__ == "__main__":
    main()
