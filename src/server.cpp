#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

struct Headers {
  std::string host;
  std::string user_agent;
  std::string accept;
  std::string content_type;
  std::string content_length;

  std::string to_string() {
    return "Host: " + host + "\r\n" + "User-Agent: " + user_agent + "\r\n" + "Accept: " + accept +
        "\r\n" + "Content-Type: " + content_type + "\r\n" + "Content-Length: " + content_length +
        "\r\n";
  }

  struct Headers Default() {
    struct Headers headers;
    headers.host           = "localhost:4221";
    headers.user_agent     = "curl/7.88.1";
    headers.accept         = "text/plain";
    headers.content_type   = "text/plain";
    headers.content_length = "0";

    return std::move(headers);
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

  struct Response Default() {
    struct Response response;
    response.method  = "HTTP/1.1";
    response.path    = "200";
    response.version = "OK";
    response.headers = response.headers.Default();
    response.body    = "";

    return std::move(response);
  }

  struct Response NotFound() {
    struct Response response;
    response.method  = "HTTP/1.1";
    response.path    = "404";
    response.version = "Not Found";
    response.headers = response.headers.Default();

    return std::move(response);
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

  pos              = request.find("\r\n");
  std::string host = request.substr(0, pos);
  request.erase(0, pos + 2);

  pos                    = request.find("\r\n");
  std::string user_agent = request.substr(0, pos);
  request.erase(0, pos + 2);

  if (path == "/" || path == "") {
    std::string body                = user_agent.substr(12);
    struct Response response        = Response().Default();
    response.body                   = body;
    response.headers.content_length = std::to_string(response.body.length());
    std::string response_str        = response.to_string();

    send(client, response_str.c_str(), response_str.length(), 0);
  } else if (path.substr(0, 5) == "/echo") {
    struct Response response        = Response().Default();
    response.body                   = path.substr(6);
    response.headers.content_length = std::to_string(response.body.length());
    std::string response_str        = response.to_string();
    send(client, response_str.c_str(), response_str.length(), 0);
  } else {
    const std::string response = Response().NotFound().to_string();
    send(client, response.c_str(), response.length(), 0);
  }

  close(server_fd);

  return 0;
}
