#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;

const std::map<std::string, uint16_t> kTypeCodes = {
    {"A", 1}, {"NS", 2}, {"CNAME", 5}, {"MX", 15}, {"TXT", 16}, {"AAAA", 28},
};

struct Answer {
  std::vector<std::string> records;
  int rcode = 0;
  uint32_t min_ttl = 0;
};

struct CacheEntry {
  std::vector<std::string> records;
  Clock::time_point expires;
};

uint8_t byte_at(const Bytes& msg, size_t offset) {
  if (offset >= msg.size()) {
    throw std::runtime_error("сообщение обрезано");
  }
  return msg[offset];
}

uint16_t read_u16(const Bytes& msg, size_t offset) {
  return (byte_at(msg, offset) << 8) | byte_at(msg, offset + 1);
}

uint32_t read_u32(const Bytes& msg, size_t offset) {
  return (uint32_t(read_u16(msg, offset)) << 16) | read_u16(msg, offset + 2);
}

void write_u16(Bytes& msg, uint16_t value) {
  msg.push_back(value >> 8);
  msg.push_back(value & 0xff);
}

std::string read_name(const Bytes& msg, size_t& offset) {
  std::string name;
  size_t pos = offset;
  bool jumped = false;
  for (int steps = 0; steps < 128; ++steps) {
    uint8_t length = byte_at(msg, pos);
    if ((length & 0xc0) == 0xc0) {
      if (!jumped) {
        offset = pos + 2;
        jumped = true;
      }
      pos = read_u16(msg, pos) & 0x3fff;
      continue;
    }
    if (length == 0) {
      if (!jumped) {
        offset = pos + 1;
      }
      return name.empty() ? "." : name;
    }
    for (size_t i = 0; i < length; ++i) {
      name += static_cast<char>(byte_at(msg, pos + 1 + i));
    }
    name += ".";
    pos += 1 + length;
  }
  throw std::runtime_error("слишком много указателей в имени");
}

std::string format_ipv6(const Bytes& msg, size_t offset) {
  uint16_t groups[8];
  for (int i = 0; i < 8; ++i) {
    groups[i] = read_u16(msg, offset + i * 2);
  }

  int best_start = -1, best_len = 0;
  for (int i = 0; i < 8;) {
    if (groups[i] != 0) {
      ++i;
      continue;
    }
    int start = i;
    while (i < 8 && groups[i] == 0) {
      ++i;
    }
    if (i - start > best_len) {
      best_start = start;
      best_len = i - start;
    }
  }
  if (best_len < 2) {
    best_start = -1;
  }

  std::string result;
  char buf[5];
  for (int i = 0; i < 8; ++i) {
    if (i == best_start) {
      result += "::";
      i += best_len - 1;
      continue;
    }
    if (!result.empty() && result.back() != ':') {
      result += ":";
    }
    std::snprintf(buf, sizeof(buf), "%x", groups[i]);
    result += buf;
  }
  return result;
}

std::string format_record(const Bytes& msg, uint16_t type, size_t data, uint16_t length) {
  switch (type) {
    case 1:
      return std::to_string(byte_at(msg, data)) + "." + std::to_string(byte_at(msg, data + 1)) +
             "." + std::to_string(byte_at(msg, data + 2)) + "." +
             std::to_string(byte_at(msg, data + 3));
    case 28:
      return format_ipv6(msg, data);
    case 2:
    case 5:
      return read_name(msg, data);
    case 15: {
      std::string priority = std::to_string(read_u16(msg, data));
      size_t name_offset = data + 2;
      return priority + " " + read_name(msg, name_offset);
    }
    case 16: {
      std::string text;
      size_t pos = data;
      while (pos < data + length) {
        uint8_t part = byte_at(msg, pos);
        for (size_t i = 0; i < part; ++i) {
          text += static_cast<char>(byte_at(msg, pos + 1 + i));
        }
        pos += 1 + part;
      }
      return text;
    }
  }
  return "";
}

std::string type_name(uint16_t code) {
  for (const auto& [name, value] : kTypeCodes) {
    if (value == code) {
      return name;
    }
  }
  return "";
}

Bytes build_query(uint16_t id, const std::string& name, uint16_t type) {
  Bytes msg;
  write_u16(msg, id);
  write_u16(msg, 0x0100);
  write_u16(msg, 1);
  write_u16(msg, 0);
  write_u16(msg, 0);
  write_u16(msg, 0);

  std::stringstream labels(name);
  std::string label;
  while (std::getline(labels, label, '.')) {
    if (label.empty()) {
      continue;
    }
    msg.push_back(label.size());
    msg.insert(msg.end(), label.begin(), label.end());
  }
  msg.push_back(0);

  write_u16(msg, type);
  write_u16(msg, 1);
  return msg;
}

Answer parse_response(const Bytes& msg) {
  Answer answer;
  answer.rcode = read_u16(msg, 2) & 0x0f;
  uint16_t questions = read_u16(msg, 4);
  uint16_t answers = read_u16(msg, 6);

  size_t offset = 12;
  for (int i = 0; i < questions; ++i) {
    read_name(msg, offset);
    offset += 4;
  }

  bool first = true;
  for (int i = 0; i < answers; ++i) {
    read_name(msg, offset);
    uint16_t type = read_u16(msg, offset);
    uint32_t ttl = read_u32(msg, offset + 4);
    uint16_t length = read_u16(msg, offset + 8);
    size_t data = offset + 10;
    offset = data + length;
    if (offset > msg.size()) {
      throw std::runtime_error("данные записи выходят за конец сообщения");
    }

    if (first || ttl < answer.min_ttl) {
      answer.min_ttl = ttl;
      first = false;
    }

    std::string name = type_name(type);
    if (!name.empty()) {
      answer.records.push_back(name + " " + format_record(msg, type, data, length) + " " +
                               std::to_string(ttl));
    }
  }
  return answer;
}

bool ask_server(int sock, const std::string& name, uint16_t type, Answer& answer) {
  static std::mt19937 random(std::random_device{}());
  uint16_t id = random() & 0xffff;
  Bytes query = build_query(id, name, type);
  send(sock, query.data(), query.size(), 0);

  auto deadline = Clock::now() + std::chrono::seconds(5);
  Bytes buffer(65536);
  while (true) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    if (left.count() <= 0) {
      return false;
    }
    pollfd fd = {sock, POLLIN, 0};
    if (poll(&fd, 1, static_cast<int>(left.count())) <= 0) {
      continue;
    }

    ssize_t received = recv(sock, buffer.data(), buffer.size(), 0);
    if (received < 12) {
      continue;
    }
    Bytes msg(buffer.begin(), buffer.begin() + received);
    if (read_u16(msg, 0) != id || !(msg[2] & 0x80)) {
      continue;
    }
    try {
      answer = parse_response(msg);
      return true;
    } catch (const std::exception& e) {
      std::cerr << "некорректный ответ: " << e.what() << "\n";
    }
  }
}

std::string status_name(int rcode) {
  switch (rcode) {
    case 0:
      return "NOERROR";
    case 1:
      return "FORMERR";
    case 2:
      return "SERVFAIL";
    case 3:
      return "NXDOMAIN";
    case 5:
      return "REFUSED";
  }
  return "RCODE" + std::to_string(rcode);
}

std::string lowercase(std::string text) {
  for (char& ch : text) {
    ch = std::tolower(static_cast<unsigned char>(ch));
  }
  return text;
}

void print_records(const std::vector<std::string>& records) {
  for (const auto& record : records) {
    std::cout << "answer " << record << "\n";
  }
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "использование: " << argv[0] << " <адрес сервера> <порт>\n";
    return EXIT_FAILURE;
  }

  sockaddr_in server = {};
  server.sin_family = AF_INET;
  server.sin_port = htons(std::atoi(argv[2]));
  if (inet_pton(AF_INET, argv[1], &server.sin_addr) != 1) {
    std::cerr << "неверный адрес сервера: " << argv[1] << "\n";
    return EXIT_FAILURE;
  }

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    std::perror("socket");
    return EXIT_FAILURE;
  }
  if (connect(sock, reinterpret_cast<sockaddr*>(&server), sizeof(server)) < 0) {
    std::perror("connect");
    return EXIT_FAILURE;
  }

  std::map<std::string, CacheEntry> cache;
  std::string line;
  while (std::getline(std::cin, line)) {
    std::stringstream stream(line);
    std::string name, type;
    if (!(stream >> name)) {
      continue;
    }
    stream >> type;

    auto type_code = kTypeCodes.find(type);
    if (type_code == kTypeCodes.end()) {
      std::cerr << "неподдерживаемый тип: " << type << "\n";
      continue;
    }

    std::cout << "query " << name << " " << type << "\n";

    std::string key = lowercase(name) + " " + type;
    auto cached = cache.find(key);
    if (cached != cache.end() && Clock::now() < cached->second.expires) {
      std::cout << "status NOERROR\n";
      print_records(cached->second.records);
      std::cout << "end" << std::endl;
      continue;
    }

    Answer answer;
    if (!ask_server(sock, name, type_code->second, answer)) {
      std::cout << "status TIMEOUT\nend" << std::endl;
      close(sock);
      return EXIT_FAILURE;
    }

    std::cout << "status " << status_name(answer.rcode) << "\n";
    print_records(answer.records);
    std::cout << "end" << std::endl;

    if (answer.rcode == 0 && !answer.records.empty() && answer.min_ttl > 0) {
      cache[key] = {answer.records, Clock::now() + std::chrono::seconds(answer.min_ttl)};
    }
  }

  close(sock);
  return EXIT_SUCCESS;
}
