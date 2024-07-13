#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

//------------ Global Variables ------------
std::mutex queue_mutex;
std::condition_variable cv;
std::queue<int> client_queue;
std::atomic<bool> running(true);
std::string dir_path = "";

struct Headers {
  std::string host;
  std::string user_agent;
  std::string accept;
  std::string content_type;
  std::string content_length;
  std::unordered_map<std::string, std::string> other_headers;

  Headers() {
    host           = "localhost:4221";
    user_agent     = "curl/7.88.1";
    accept         = "*/*";
    content_type   = "text/plain";
    content_length = "0";
    other_headers  = {};
  }

  std::string to_string() const {
    auto other_headers_to_string = [&]() {
      std::string result = "";
      for (const auto &header : other_headers) {
        result += header.first + ": " + header.second + "\r\n";
      }
      return result;
    };
    return "Host: " + host + "\r\n" + "User-Agent: " + user_agent + "\r\n" + "Accept: " + accept +
        "\r\n" + "Content-Type: " + content_type + "\r\n" + "Content-Length: " + content_length +
        "\r\n" + other_headers_to_string();
  }
};

struct Response {
  std::string version;
  std::string status_code;
  std::string reason_phrase;
  Headers headers;
  std::string body;

  Response() {
    version       = "HTTP/1.1";
    status_code   = "200";
    reason_phrase = "OK";
    headers       = Headers();
    body          = "";
  }

  static Response NotFound() {
    Response response;
    response.status_code   = "404";
    response.reason_phrase = "Not Found";
    response.body          = "404 Not Found";
    return response;
  }

  std::string to_string() const {
    return version + " " + status_code + " " + reason_phrase + "\r\n" + headers.to_string() +
        "\r\n" + body;
  }
};

struct Request {
  std::string method;
  std::string path;
  std::string version;
  Headers headers;
  std::string body;

  Request() {
    method  = "GET";
    path    = "/";
    version = "HTTP/1.1";
    headers = Headers();
    body    = "";
  }
  std::string to_string() const {
    return method + " " + path + " " + version + "\r\n" + headers.to_string() + "\r\n" + body;
  }
};

//------------ Function Declarations ------------

void worker_thread();
void signal_handler(int signum);
void handle_client(int client_fd);
struct Request parse_request(const std::string &request_str);
void routing_logic(const int &client_fd, const Request &request);
std::string compress_string(const std::string &str);

//------------ Function Definitions ------------

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  if (argc == 3) {
    dir_path = argv[2];
  }

  std::cout << "Logs from your program will appear here!\n";

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    close(server_fd);
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family      = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port        = htons(4221);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 4221\n";
    close(server_fd);
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    close(server_fd);
    return 1;
  }

  std::cout << "Waiting for a client to connect...\n";

  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  const int num_threads = 6;
  std::vector<std::thread> threads;
  for (int i = 0; i < num_threads; i++) {
    threads.emplace_back(worker_thread);
  }

  while (running) {
    int client = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client < 0) {
      std::cerr << "Failed to accept client connection\n";
      continue;
    }
    std::cout << "Client connected with client_id : " << client << std::endl;
    {
      std::lock_guard<std::mutex> lock(queue_mutex);
      client_queue.push(client);
    }
    cv.notify_one();
  }

  for (auto &thread : threads) {
    thread.join();
  }

  close(server_fd);
  return 0;
}

void handle_client(int client_fd) {
  char buffer[1024] = {0};

  if (recv(client_fd, buffer, 1024, 0) < 0) {
    std::cerr << "Failed to receive data\n";
    return;
  }

  std::string request_str(buffer);
  Request request = parse_request(request_str);
  std::cout << request.to_string() << std::endl;
  routing_logic(client_fd, request);
}

void routing_logic(const int &client_fd, const Request &request) {
  struct Response response = Response();
  if (request.method == "GET") {
    goto GET_METHODS;
  } else if (request.method == "POST") {
    goto POST_METHODS;
  } else {
    struct Response response = Response();
    response.status_code     = "405";
    response.reason_phrase   = "Method Not Allowed";
    std::string response_str = response.to_string();
    send(client_fd, response_str.c_str(), response_str.length(), 0);
    return;
  }
  // Under this line, we have GET methods
GET_METHODS:
  if (request.path == "/" || request.path == "") {
    goto SEND_RESPONSE;
  } else if (request.path == "/user-agent") {
    response.body                   = request.headers.user_agent;
    response.headers.content_length = std::to_string(response.body.length());
    goto SEND_RESPONSE;
  } else if (request.path.substr(0, 5) == "/echo") {
    response.body                   = request.path.substr(6);
    response.headers.content_length = std::to_string(response.body.length());
    goto SEND_RESPONSE;
  } else if (request.path.substr(0, 6) == "/files") {
    std::string file_path = dir_path + request.path.substr(6);
    std::cout << file_path << std::endl;
    std::ifstream file(file_path, std::ios::out);
    if (file.is_open()) {
      std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
      response.body                   = content;
      response.headers.content_type   = "application/octet-stream";
      response.headers.content_length = std::to_string(response.body.length());
      file.close();
      goto SEND_RESPONSE;
    } else {
      response = Response::NotFound();
      goto SEND_RESPONSE;
    }
  } else {
    response = Response::NotFound();
    goto SEND_RESPONSE;
  }
  return;

  // Under this line, we have POST methods
POST_METHODS:
  if (request.path.substr(0, 6) == "/files") {
    std::string file_path = dir_path + request.path.substr(6);
    std::ofstream file(file_path);
    if (file.is_open()) {
      file << request.body;
      response.status_code            = "201";
      response.reason_phrase          = "Created";
      response.headers.content_length = std::to_string(response.body.length());
      file.close();
      goto SEND_RESPONSE;
    } else {
      response = Response::NotFound();
      goto SEND_RESPONSE;
    }
  } else {
    response = Response::NotFound();
    goto SEND_RESPONSE;
  }

SEND_RESPONSE:
  auto other_headers = request.headers.other_headers;
  if (other_headers.find("Accept-Encoding") != other_headers.end() &&
      other_headers["Accept-Encoding"].find("gzip") != std::string::npos) {
    response.headers.other_headers["Content-Encoding"] = "gzip";
    response.body                                      = compress_string(response.body);
    response.headers.content_length                    = std::to_string(response.body.length());
  }
  std::string response_str = response.to_string();
  send(client_fd, response_str.c_str(), response_str.length(), 0);
}

void worker_thread() {
  while (running) {
    std::unique_lock<std::mutex> lock(queue_mutex);
    cv.wait(lock, [] { return !client_queue.empty() || !running; });

    if (!running && client_queue.empty()) {
      return;
    }

    int client_socket = client_queue.front();
    client_queue.pop();
    lock.unlock();

    handle_client(client_socket);
  }
}

void signal_handler(int signum) {
  running = false;
  cv.notify_all();
}

struct Request parse_request(const std::string &request_str) {
  std::istringstream request_stream(request_str);
  std::string line;

  // Parse the request line
  std::getline(request_stream, line);
  std::istringstream request_line_stream(line);
  std::string method, path, version;
  request_line_stream >> method >> path >> version;

  Headers headers;
  // Parse headers
  while (std::getline(request_stream, line) && line != "\r") {
    std::istringstream header_line_stream(line);
    std::string key;
    std::getline(header_line_stream, key, ':');
    std::string value;
    std::getline(header_line_stream, value);

    auto trim = [](std::string &s) -> std::string {
      return s.erase(0, s.find_first_not_of(" \t\r\n")).erase(s.find_last_not_of(" \t\r\n") + 1);
    };

    key   = trim(key);
    value = trim(value);

    if (key == "Host") {
      headers.host = value;
    } else if (key == "User-Agent") {
      headers.user_agent = value;
    } else if (key == "Accept") {
      headers.accept = value;
    } else if (key == "Content-Type") {
      headers.content_type = value;
    } else if (key == "Content-Length") {
      headers.content_length = value;
    } else {
      headers.other_headers[key] = value;
    }
  }

  // Parse body
  std::string body;
  if (headers.content_length != "") {
    int content_length = std::stoi(headers.content_length);
    body.resize(content_length);
    request_stream.read(&body[0], content_length);
  }

  struct Request request = Request();
  request.method         = method;
  request.path           = path;
  request.headers        = headers;
  request.body           = body;

  return request;
}

std::string compress_string(const std::string &str) {
  uLongf dest_len = compressBound(str.length());
  Bytef *dest     = new Bytef[dest_len];
  compress(dest, &dest_len, (const Bytef *)str.c_str(), str.length());

  std::string compressed_str((char *)dest, dest_len);
  delete[] dest;
  return compressed_str;
}
