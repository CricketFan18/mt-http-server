#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <string>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <sys/time.h>
#include <sstream>
#include <fstream>

static constexpr int BACKLOG = SOMAXCONN;

struct HttpRequest
{
    std::string method;
    std::string path;
    bool is_logged_in = false;
};

struct HttpResponse
{
    int status_code = 200;
    std::string content_type = "text/html";
    std::string body;
    std::string extra_headers = "";
};

// Global flag to control the loop
volatile sig_atomic_t server_running = 1; // volatile forces the code to read the value from RAM every single time.

// Signal handler function
void handle_sigint(int sig)
{
    server_running = 0;
}

void install_shutdown()
{
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // disable SA_RESTART
    if (sigaction(SIGINT, &sa, nullptr) == -1)
    {
        perror("sigaction");
        exit(1);
    }
}

HttpRequest parse_request(const char *buffer, int bytes_read)
{
    HttpRequest req;
    std::string raw_text(buffer, bytes_read);
    std::stringstream ss(raw_text); // works like String Tokenizer from java but has type conversion, gives string after each whitespace,

    ss >> req.method >> req.path; // Parse Method and Path

    if (raw_text.find("Cookie: session_token=secretkey12345") != std::string::npos) // Parse Cookies
    {
        req.is_logged_in = true;
    }

    return req;
}

std::string get_cpu_stats()
{
    std::ifstream f("/proc/loadavg");
    if (!f.good())
        return "Error: Not on Linux";
    std::string line;
    std::getline(f, line);
    return "<h1>Kernel Load: </h1>"
           "<h2>" +
           line + "</h2>";
}

std::string read_file(const std::string &filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf(); // Read entire file into string stream
    return buffer.str();
}

HttpResponse handle_routing(HttpRequest &req)
{
    HttpResponse res;
    if (req.path == "/")
    {
        std::string html_content = read_file("index.html");
        if (!html_content.empty())
        {
            res.body = html_content;
        }
        else
        {
            res.status_code = 404;
            res.body = "<h1>404 Error</h1><p>index.html not found on server.</p>";
        }
    }
    else if (req.path == "/cpu")
    {
        res.body = get_cpu_stats();
    }
    else if (req.path == "/info")
    {
        res.content_type = "application/json";
        res.body = "{\"purpose\": \"to build own server\", \"experience\": \"its fantastic\", \"learning\": \"how system calls and networks work\"}";
    }
    else if (req.path == "/login" && req.method == "POST")
    {
        res.body = "<h1>Login Successful</h1>";
        res.extra_headers = "Set-Cookie: session_token=secretkey12345; Path=/; HttpOnly\r\n";
    }
    else if (req.path == "/dashboard")
    {
        if (req.is_logged_in)
        {
            res.body = "<h1>Admin Dashboard</h1><p>Secure Area.</p>";
        }
        else
        {
            res.status_code = 403;
            res.body = "<h1>403 Forbidden: Please POST /login</h1>";
        }
    }
    else if (req.path == "/logout")
    {
        if (req.is_logged_in)
        {
            res.body = "<h1>You are logged out</h1>";
            req.is_logged_in = false;
            res.extra_headers = "Set-Cookie: session_token=; Path=/; Max-Age=0; HttpOnly\r\n";
        }
        else
        {
            res.status_code = 403;
            res.body = "<h1>403 Forbidden: Please POST /login</h1>";
        }
    }
    else
    {
        res.status_code = 404;
        res.body = "<h1>404 Not Found</h1>";
    }
    return res;
}

std::string status_text(int code)
{
    switch (code)
    {
    case 200:
        return "OK";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 503:
        return "Service Unavailable";
    default:
        return "Error";
    }
}

void send_response(int client_fd, const HttpResponse &res)
{
    std::string response_str =
        "HTTP/1.1 " + std::to_string(res.status_code) + " " + status_text(res.status_code) + "\r\n" +
        "Content-Type: " + res.content_type + "\r\n" +
        "Content-Length: " + std::to_string(res.body.size()) + "\r\n" +
        "Connection: keep-alive\r\n" +
        "Keep-Alive: timeout=5, max=100\r\n" +
        res.extra_headers +
        "\r\n" +
        res.body;

    send(client_fd, response_str.c_str(), response_str.size(), 0);
}

class WorkerPool
{
public:
    WorkerPool(size_t n = std::thread::hardware_concurrency())
    {
        if (n == 0)
            n = 4;
        for (size_t i = 0; i < n; i++)
        {
            workers_.emplace_back([this]
                                  { 
                while(true)
                {
                    int new_client = -1;
                    { // Critical Section
                        std::unique_lock<std::mutex> lk(qmutex_); // Acquire lock
                        cv_.wait(lk, [this] { return stop_ || !clients_.empty(); }); // Condition for wait

                        if (stop_ && clients_.empty()) { // exit if pool is stopped and no tasks
                            return;
                        }

                        new_client = clients_.front(); clients_.pop();
                        std::cout << new_client << " is being served by " << std::this_thread::get_id() << std::endl;
                    }
                    handle_client(new_client);
                } });
        }
        std::cout << "Created Pool of size " << n << std::endl;
    }

    bool add_client(int new_client)
    {
        {                                             // Critical Section
            std::unique_lock<std::mutex> lk(qmutex_); // Acquire lock
            if (clients_.size() >= max_queue_size_)
                return false;

            clients_.emplace(new_client); // Push to queue
        }
        cv_.notify_one();
        return true;
    }

    ~WorkerPool()
    {
        { // Lock the queue to update the stop flag safely
            std::unique_lock<std::mutex> lk(qmutex_);
            stop_ = true;
        }

        cv_.notify_all(); // Notify all threads

        for (auto &thread : workers_) // Joining all worker threads to ensure they have completed their task
        {
            if (thread.joinable())
                thread.join();
        }
    }

private:
    std::queue<int> clients_;
    std::mutex qmutex_;
    std::condition_variable cv_;
    std::vector<std::thread> workers_;
    bool stop_ = false;
    const size_t max_queue_size_ = 1000;
    std::mutex log_mutex;

    void safe_log(const std::string &message) // prevents inter_leaving
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cout << "[Server] " << message << std::endl;
    }

    void handle_client(int client_fd)
    {
        struct timeval tv{5, 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv)); // Setup Timeout

        while (true)
        {
            // Receive Data
            char buffer[4096] = {0};
            int bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);

            if (bytes_read <= 0)
                break; // Closed or Timeout

            HttpRequest req = parse_request(buffer, bytes_read); // Parse
            safe_log("Thread " + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + " handling client");
            HttpResponse res = handle_routing(req);

            send_response(client_fd, res);
        }
        close(client_fd);
    }
};

//-------------------------------------------//
//             Socket  Code
//-------------------------------------------//

static const void *get_ip(const sockaddr *addr)
{
    if (addr->sa_family == AF_INET) // IPv4
        return &reinterpret_cast<const sockaddr_in *>(addr)->sin_addr;

    return &reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_addr; // IPv6
}

static uint16_t get_port(sockaddr *addr)
{
    if (addr->sa_family == AF_INET) // IPv4
        return ntohs(reinterpret_cast<sockaddr_in *>(addr)->sin_port);

    return ntohs(reinterpret_cast<sockaddr_in6 *>(addr)->sin6_port); // IPv6
}

static std::string print_ip(sockaddr *addr)
{
    char ipstr[INET6_ADDRSTRLEN];
    if (!inet_ntop(addr->sa_family, get_ip(addr), ipstr, sizeof(ipstr)))
        return "<invalid address>";

    if (addr->sa_family == AF_INET6)
        return "[" + std::string(ipstr) + "]:" + std::to_string(get_port(addr));

    return std::string(ipstr) + ":" + std::to_string(get_port(addr));
}

int setup_server(const char *port)
{
    addrinfo hints{}, *res = nullptr, *p;
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;

    int rv = getaddrinfo(nullptr, port, &hints, &res);
    if (rv != 0)
    {
        std::cerr << "gai error: " << gai_strerror(rv) << std::endl;
        std::exit(1);
    }
    int sockfd = -1;
    for (p = res; p != nullptr; p = p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1)
        {
            std::perror("server: socket");
            continue;
        }
        int yes = 1;
        if ((setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1))
        {
            std::perror("server: socket options");
            close(sockfd);
            sockfd = -1;
            continue;
        }
        if ((bind(sockfd, p->ai_addr, p->ai_addrlen) == -1))
        {
            std::perror("server: bind");
            close(sockfd);
            sockfd = -1;
            continue;
        }
        if ((listen(sockfd, BACKLOG) == -1))
        {
            std::perror("server: listen");
            close(sockfd);
            sockfd = -1;
            continue;
        }
        std::cout << "Server started on - " + print_ip(p->ai_addr) << std::endl;
        break;
    }
    freeaddrinfo(res);
    if (sockfd == -1)
    {
        std::cerr << "Server failed to setup.\n";
        std::exit(2);
    }
    return sockfd;
}

int main(int argc, char *argv[])
{
    const char* port = "8080" ;
    int pool_size = 5;
    if (argc > 1) port = argv[1];
    if (argc > 2) pool_size = std::atoi(argv[2]);

    signal(SIGPIPE, SIG_IGN); // handle if client disconnects while writing to them, ignore signal if pipe broken
    install_shutdown();
    WorkerPool pool(pool_size);
    int listener = setup_server(port);

    while (server_running)
    {
        sockaddr_storage their_addr{};
        socklen_t sin_size = sizeof(their_addr);
        int new_client = accept(listener, reinterpret_cast<sockaddr *>(&their_addr), &sin_size);

        if (new_client == -1)
        {
            if (errno == EINTR) // If accept was interrupted by Ctrl+C, break the loop
            {
                break;
            }
            std::perror("server: accept");
            continue;
        }
        std::cout << "Got connection from - " + print_ip(reinterpret_cast<sockaddr *>(&their_addr)) << std::endl;

        // --- Handle Client ---
        if (!pool.add_client(new_client))
        {
            const std::string body = "Server is too busy. Try again later.";
            std::string error_msg =
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: " +
                std::to_string(body.size()) + "\r\n"
                                              "Connection: close\r\n"
                                              "\r\n" +
                body;

            send(new_client, error_msg.c_str(), error_msg.size(), 0);
            close(new_client);

            std::cout << "Dropped client (Queue Full)" << std::endl;
        }
    }

    // Graceful shutdown
    std::cout << "\nShutting down server..." << std::endl;
    close(listener);
    std::cout << "Bye!" << std::endl;

    return 0;
}