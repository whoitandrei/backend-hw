#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>

using Clock = std::chrono::steady_clock;

const int kBaseMs = 200;
const int kCapMs = 2000;
const auto kAttemptTimeout = std::chrono::seconds(5);

struct Url {
  std::string host;
  std::string port = "80";
  std::string path = "/";
};

struct Options {
  Url url;
  std::string method = "GET";
  int max_attempts = 5;
  std::string idempotency_key;
};

struct AttemptResult {
  bool answered = false;
  int status = 0;
  int retry_after_seconds = -1;
  std::string error;
};

bool parse_url(const std::string& text, Url& url) {
  const std::string scheme = "http://";
  if (text.rfind(scheme, 0) != 0) {
    return false;
  }
  std::string rest = text.substr(scheme.size());

  size_t slash = rest.find('/');
  if (slash != std::string::npos) {
    url.path = rest.substr(slash);
    rest = rest.substr(0, slash);
  }

  size_t colon = rest.find(':');
  if (colon != std::string::npos) {
    url.port = rest.substr(colon + 1);
    rest = rest.substr(0, colon);
  }
  url.host = rest;
  return !url.host.empty() && !url.port.empty();
}

bool parse_options(int argc, char* argv[], Options& options) {
  bool has_url = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    bool has_value = i + 1 < argc;
    if (arg == "--method" && has_value) {
      options.method = argv[++i];
    } else if (arg == "--max-attempts" && has_value) {
      options.max_attempts = std::atoi(argv[++i]);
      if (options.max_attempts < 1) {
        return false;
      }
    } else if (arg == "--idempotency-key" && has_value) {
      options.idempotency_key = argv[++i];
    } else if (!has_url && parse_url(arg, options.url)) {
      has_url = true;
    } else {
      return false;
    }
  }
  return has_url;
}

bool wait_for(int sock, short events, Clock::time_point deadline) {
  auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
  if (left.count() <= 0) {
    return false;
  }
  pollfd fd = {sock, events, 0};
  return poll(&fd, 1, static_cast<int>(left.count())) > 0;
}

int connect_to(const Url& url, Clock::time_point deadline, std::string& error) {
  addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* address = nullptr;
  int code = getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &address);
  if (code != 0) {
    error = gai_strerror(code);
    return -1;
  }

  int sock = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
  if (sock < 0) {
    error = std::strerror(errno);
    freeaddrinfo(address);
    return -1;
  }
  fcntl(sock, F_SETFL, O_NONBLOCK);
  int result = connect(sock, address->ai_addr, address->ai_addrlen);
  freeaddrinfo(address);

  if (result < 0 && errno != EINPROGRESS) {
    error = std::strerror(errno);
    close(sock);
    return -1;
  }
  if (result < 0) {
    if (!wait_for(sock, POLLOUT, deadline)) {
      error = "connect timeout";
      close(sock);
      return -1;
    }
    int socket_error = 0;
    socklen_t length = sizeof(socket_error);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &socket_error, &length);
    if (socket_error != 0) {
      error = std::strerror(socket_error);
      close(sock);
      return -1;
    }
  }
  return sock;
}

bool send_all(int sock, const std::string& data, Clock::time_point deadline, std::string& error) {
  size_t sent = 0;
  while (sent < data.size()) {
    if (!wait_for(sock, POLLOUT, deadline)) {
      error = "send timeout";
      return false;
    }
    ssize_t count = send(sock, data.data() + sent, data.size() - sent, 0);
    if (count < 0 && errno != EAGAIN) {
      error = std::strerror(errno);
      return false;
    }
    if (count > 0) {
      sent += count;
    }
  }
  return true;
}

bool receive_all(int sock, std::string& data, Clock::time_point deadline, std::string& error) {
  char buffer[4096];
  while (true) {
    if (!wait_for(sock, POLLIN, deadline)) {
      error = "read timeout";
      return false;
    }
    ssize_t count = recv(sock, buffer, sizeof(buffer), 0);
    if (count == 0) {
      return true;
    }
    if (count < 0 && errno != EAGAIN) {
      error = std::strerror(errno);
      return false;
    }
    if (count > 0) {
      data.append(buffer, count);
    }
  }
}

std::string lowercase(std::string text) {
  for (char& ch : text) {
    ch = std::tolower(static_cast<unsigned char>(ch));
  }
  return text;
}

bool parse_response(const std::string& data, AttemptResult& result) {
  std::stringstream stream(data);
  std::string line;
  if (!std::getline(stream, line) || line.rfind("HTTP/", 0) != 0 || line.size() < 12) {
    return false;
  }
  std::string status = line.substr(9, 3);
  if (!std::all_of(status.begin(), status.end(), ::isdigit)) {
    return false;
  }
  result.status = std::stoi(status);

  while (std::getline(stream, line) && line != "\r" && !line.empty()) {
    size_t colon = line.find(':');
    if (colon == std::string::npos || lowercase(line.substr(0, colon)) != "retry-after") {
      continue;
    }
    std::string value;
    std::stringstream(line.substr(colon + 1)) >> value;
    if (!value.empty() && std::all_of(value.begin(), value.end(), ::isdigit)) {
      result.retry_after_seconds = std::stoi(value);
    }
  }
  return true;
}

AttemptResult make_attempt(const Options& options) {
  AttemptResult result;
  auto deadline = Clock::now() + kAttemptTimeout;

  int sock = connect_to(options.url, deadline, result.error);
  if (sock < 0) {
    return result;
  }

  std::string request = options.method + " " + options.url.path + " HTTP/1.1\r\n";
  request += "Host: " + options.url.host + ":" + options.url.port + "\r\n";
  request += "Connection: close\r\n";
  request += "Content-Length: 0\r\n";
  if (!options.idempotency_key.empty()) {
    request += "Idempotency-Key: " + options.idempotency_key + "\r\n";
  }
  request += "\r\n";

  std::string response;
  if (send_all(sock, request, deadline, result.error) &&
      receive_all(sock, response, deadline, result.error)) {
    result.answered = parse_response(response, result);
    if (!result.answered) {
      result.error = "bad response";
    }
  }
  close(sock);
  return result;
}

bool is_success(const AttemptResult& result) {
  return result.answered && result.status >= 200 && result.status <= 399;
}

bool is_retryable(const AttemptResult& result) {
  if (!result.answered) {
    return true;
  }
  static const std::set<int> retry_codes = {429, 500, 502, 503, 504};
  return retry_codes.count(result.status) > 0;
}

bool may_repeat(const Options& options) {
  static const std::set<std::string> idempotent = {"GET",    "HEAD",    "PUT",
                                                   "DELETE", "OPTIONS", "TRACE"};
  return idempotent.count(options.method) > 0 || !options.idempotency_key.empty();
}

int backoff_ceiling_ms(int next_attempt) {
  int interval = kBaseMs;
  for (int n = 1; n < next_attempt && interval < kCapMs; ++n) {
    interval *= 2;
  }
  return std::min(interval, kCapMs);
}

int main(int argc, char* argv[]) {
  Options options;
  if (!parse_options(argc, argv, options)) {
    std::cerr << "использование: " << argv[0]
              << " http://<хост>[:<порт>]/<путь> [--method <метод>] [--max-attempts <число>]"
                 " [--idempotency-key <значение>]\n";
    return 2;
  }

  std::signal(SIGPIPE, SIG_IGN);
  std::mt19937 random(std::random_device{}());
  AttemptResult result;
  int attempt = 1;
  while (true) {
    result = make_attempt(options);
    if (result.answered) {
      std::cout << "attempt " << attempt << " status " << result.status << std::endl;
    } else {
      std::cout << "attempt " << attempt << " error " << result.error << std::endl;
    }

    if (is_success(result) || !is_retryable(result) || !may_repeat(options) ||
        attempt >= options.max_attempts) {
      break;
    }

    int pause_ms;
    if (result.retry_after_seconds >= 0) {
      pause_ms = result.retry_after_seconds * 1000;
    } else {
      std::uniform_int_distribution<int> spread(0, backoff_ceiling_ms(attempt + 1));
      pause_ms = spread(random);
    }
    std::cout << "sleep_ms " << pause_ms << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(pause_ms));
    ++attempt;
  }

  bool success = is_success(result);
  std::cout << "result " << (success ? "success" : "failure") << " attempts " << attempt
            << std::endl;
  return success ? 0 : 1;
}
