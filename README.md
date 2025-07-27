# HTTP Proxy Server (C)

This is a multi-threaded HTTP proxy server written in C. It receives client HTTP requests, forwards them to destination servers, and sends back responses. It supports:

- URL and IP filtering using a blocklist file
- Thread pool for concurrent handling
- Request limit enforcement

🛠️ **Author**: Raghad Alyan  

---

## 📂 Project Structure

- `proxyServer.c` – Main proxy logic (handling requests, filtering, forwarding).
- `threadpool.c` – Thread pool implementation.
- `threadpool.h` – Thread pool header file.

---

## 🧠 Features

### ✅ Filtering
- Loads a filter list of domains or IP subnets.
- Blocks matching requests with a `403 Forbidden` response.

### ✅ Thread Pool
- Handles multiple clients concurrently.
- Threads are managed efficiently using a preallocated pool.

### ✅ Request Limit
- Supports limiting the number of total requests (max queue).

---

## 🔧 Key Functions

### In `proxyServer.c`:
- `read_filter_file()` – Loads filter rules from file.
- `check_url_against_filter()` – Validates requests against filters.
- `handle_request()` – Parses and forwards requests, handles errors.
- `send_400/403/404/501_error_response()` – Sends proper HTTP error responses.
- `listen_for_requests()` – Accepts incoming clients and dispatches them to the thread pool.

### In `threadpool.c`:
- Thread pool setup, enqueueing, and worker thread execution.

---

## 🧪 Compilation

```bash
gcc -Wall -o proxyServer proxyServer.c threadpool.c -lpthread

## ▶️ Execution
./proxyServer <port> <threadpool size> <max requests> <filter file path>
