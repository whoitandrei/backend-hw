#include <cctype>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

const std::string CRLF = "\r\n";

struct ParseError {
  std::string reason;
};

struct StartLine {
  bool is_request = false;
  std::string method;
  std::string target;
  std::string version;
  std::string status;
  std::string reason;
};

struct Message {
  StartLine start;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
};

bool is_token_char(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) ||
         std::string("!#$%&'*+-.^_`|~").find(ch) != std::string::npos;
}

bool is_token(const std::string& text) {
  if (text.empty()) {
    return false;
  }
  for (char ch : text) {
    if (!is_token_char(ch)) {
      return false;
    }
  }
  return true;
}

std::string trim(const std::string& text) {
  size_t begin = text.find_first_not_of(" \t");
  if (begin == std::string::npos) {
    return "";
  }
  size_t end = text.find_last_not_of(" \t");
  return text.substr(begin, end - begin + 1);
}

std::string lowercase(std::string text) {
  for (char& ch : text) {
    ch = std::tolower(static_cast<unsigned char>(ch));
  }
  return text;
}

bool parse_number(const std::string& text, int base, size_t& value) {
  if (text.empty() || text.size() > 15) {
    return false;
  }
  for (char ch : text) {
    bool ok = base == 16 ? std::isxdigit(static_cast<unsigned char>(ch))
                         : std::isdigit(static_cast<unsigned char>(ch));
    if (!ok) {
      return false;
    }
  }
  value = std::stoull(text, nullptr, base);
  return true;
}

StartLine parse_start_line(const std::string& line) {
  if (line.rfind("HTTP/", 0) == 0) {
    size_t first_space = line.find(' ');
    if (first_space == std::string::npos) {
      throw ParseError{"bad_start_line"};
    }
    std::string version = line.substr(0, first_space);
    std::string status = line.substr(first_space + 1, 3);
    std::string rest = line.substr(first_space + 1 + status.size());
    bool status_ok = status.size() == 3 && std::isdigit(static_cast<unsigned char>(status[0])) &&
                     std::isdigit(static_cast<unsigned char>(status[1])) &&
                     std::isdigit(static_cast<unsigned char>(status[2]));
    if (version != "HTTP/1.1" || !status_ok || (!rest.empty() && rest[0] != ' ')) {
      throw ParseError{"bad_start_line"};
    }
    StartLine start;
    start.version = version;
    start.status = status;
    start.reason = rest.empty() ? "" : rest.substr(1);
    return start;
  }

  size_t first_space = line.find(' ');
  size_t second_space = line.find(' ', first_space + 1);
  if (first_space == std::string::npos || second_space == std::string::npos) {
    throw ParseError{"bad_start_line"};
  }
  std::string method = line.substr(0, first_space);
  std::string target = line.substr(first_space + 1, second_space - first_space - 1);
  std::string version = line.substr(second_space + 1);
  if (!is_token(method) || target.empty() || version != "HTTP/1.1") {
    throw ParseError{"bad_start_line"};
  }
  StartLine start;
  start.is_request = true;
  start.method = method;
  start.target = target;
  start.version = version;
  return start;
}

std::string read_chunked_body(const std::string& data, size_t pos) {
  std::string body;
  while (true) {
    size_t line_end = data.find(CRLF, pos);
    if (line_end == std::string::npos) {
      throw ParseError{"bad_chunk"};
    }
    std::string size_line = data.substr(pos, line_end - pos);
    size_line = trim(size_line.substr(0, size_line.find(';')));

    size_t size;
    if (!parse_number(size_line, 16, size)) {
      throw ParseError{"bad_chunk"};
    }
    pos = line_end + 2;
    if (size == 0) {
      return body;
    }
    if (data.size() < pos + size + 2 || data.compare(pos + size, 2, CRLF) != 0) {
      throw ParseError{"bad_chunk"};
    }
    body += data.substr(pos, size);
    pos += size + 2;
  }
}

Message parse_message(const std::string& data) {
  Message message;

  size_t line_end = data.find(CRLF);
  if (line_end == std::string::npos) {
    throw ParseError{"bad_start_line"};
  }
  message.start = parse_start_line(data.substr(0, line_end));
  size_t pos = line_end + 2;

  bool chunked = false;
  bool has_length = false;
  size_t content_length = 0;
  while (true) {
    line_end = data.find(CRLF, pos);
    if (line_end == std::string::npos) {
      throw ParseError{"bad_header"};
    }
    std::string line = data.substr(pos, line_end - pos);
    pos = line_end + 2;
    if (line.empty()) {
      break;
    }

    size_t colon = line.find(':');
    if (colon == std::string::npos || !is_token(line.substr(0, colon))) {
      throw ParseError{"bad_header"};
    }
    std::string name = lowercase(line.substr(0, colon));
    std::string value = trim(line.substr(colon + 1));
    message.headers.push_back({name, value});

    if (name == "transfer-encoding" && lowercase(value) == "chunked") {
      chunked = true;
    }
    if (name == "content-length") {
      if (!parse_number(value, 10, content_length)) {
        throw ParseError{"bad_header"};
      }
      has_length = true;
    }
  }

  if (chunked) {
    message.body = read_chunked_body(data, pos);
  } else if (has_length) {
    if (data.size() - pos < content_length) {
      throw ParseError{"incomplete_body"};
    }
    message.body = data.substr(pos, content_length);
  }
  return message;
}

bool is_printable(const std::string& text) {
  for (char ch : text) {
    if (ch < 0x20 || ch > 0x7e) {
      return false;
    }
  }
  return true;
}

int main() {
  std::string data((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());

  Message message;
  try {
    message = parse_message(data);
  } catch (const ParseError& error) {
    std::cout << "error " << error.reason << "\n";
    return EXIT_FAILURE;
  }

  const auto& start = message.start;
  std::cout << "version " << start.version << "\n";
  if (start.is_request) {
    std::cout << "type request\n";
    std::cout << "method " << start.method << "\n";
    std::cout << "target " << start.target << "\n";
  } else {
    std::cout << "type response\n";
    std::cout << "status " << start.status << "\n";
    std::cout << "reason " << start.reason << "\n";
  }

  for (const auto& [name, value] : message.headers) {
    std::cout << "header " << name << " " << value << "\n";
  }

  std::cout << "body.length " << message.body.size() << "\n";
  if (!message.body.empty() && is_printable(message.body)) {
    std::cout << "body.text " << message.body << "\n";
  }
  return EXIT_SUCCESS;
}
