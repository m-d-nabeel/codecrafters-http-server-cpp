#include <cstdlib>
#include <cstring>
#include <iostream>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct Headers {
  std::string host;
  std::string user_agent;
  std::string accept;

  std::string to_string() {
    return "Host: " + host + "\r\n" + "User-Agent: " + user_agent + "\r\n" + "Accept: " + accept +
        "\r\n";
  }

  std::string Default() {
    struct Headers headers;
    headers.host       = "Host: localhost:4221";
    headers.user_agent = "User-Agent: Mozilla/5.0";
    headers.accept     = "Accept: text/html";

    return std::move(headers.to_string());
  }
};

struct Response {
  std::string method;
  std::string path;
  std::string version;
  Headers headers;
  std::string body;

  std::string to_string() {
    return method + " " + path + " " + version + "\r\n" + headers.to_string() + "\r\n" + body;
  }

  std::string Default() {
    struct Response response;
    response.method             = "HTTP/1.1";
    response.path               = "200";
    response.version            = "OK";
    response.headers.host       = "Host: localhost:4221";
    response.headers.user_agent = "User-Agent: curl/7.88.1";
    response.headers.accept     = "Accept: */*";
    response.body               = "";

    return std::move(response.to_string());
  }

  std::string NotFound() {
    struct Response response;
    response.method             = "HTTP/1.1";
    response.path               = "404";
    response.version            = "Not Found";
    response.headers.host       = "Host: localhost:4221";
    response.headers.user_agent = "User-Agent: curl/7.88.1";
    response.headers.accept     = "Accept: */*";
    response.body               = "404 Not Found";

    return std::move(response.to_string());
  }
};

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // You can use print statements as follows for debugging, they'll be visible
  // when running tests.
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
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family      = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port        = htons(4221);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);

  std::cout << "Waiting for a client to connect...\n";

  int client = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
  std::cout << "Client connected\n";

  char buffer[1024] = {0};
  int n             = recv(client, buffer, 1024, 0);

  if (n == -1) {
    std::cerr << "Failed to receive data\n";
    return 1;
  }

  std::cout << "Received " << n << " bytes\n";

  std::string request(buffer);
  std::cout << request << std::endl;

  size_t pos               = request.find("\r\n");
  std::string request_line = request.substr(0, pos);
  request.erase(0, pos + 2);

  pos                = request_line.find(" ");
  std::string method = request_line.substr(0, pos);
  request_line.erase(0, pos + 1);

  pos              = request_line.find(" ");
  std::string path = request_line.substr(0, pos);
  request_line.erase(0, pos + 1);

  if (path == "/" || path == "") {
    const std::string response = Response().Default();
    send(client, response.c_str(), response.length(), 0);
  } else {
    const std::string response = Response().NotFound();
    send(client, response.c_str(), response.length(), 0);
  }

  close(server_fd);

  return 0;
}
