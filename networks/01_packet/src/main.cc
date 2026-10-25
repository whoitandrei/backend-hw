#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;

uint16_t read_u16(const Bytes& frame, size_t offset) {
  return (frame[offset] << 8) | frame[offset + 1];
}

uint32_t read_u32(const Bytes& frame, size_t offset) {
  return (uint32_t(frame[offset]) << 24) | (frame[offset + 1] << 16) |
         (frame[offset + 2] << 8) | frame[offset + 3];
}

std::string hex_byte(uint8_t byte) {
  char buf[3];
  std::snprintf(buf, sizeof(buf), "%02x", byte);
  return buf;
}

std::string mac(const Bytes& frame, size_t offset) {
  std::string result;
  for (size_t i = 0; i < 6; ++i) {
    if (i > 0) {
      result += ":";
    }
    result += hex_byte(frame[offset + i]);
  }
  return result;
}

std::string ipv4(const Bytes& frame, size_t offset) {
  std::string result;
  for (size_t i = 0; i < 4; ++i) {
    if (i > 0) {
      result += ".";
    }
    result += std::to_string(frame[offset + i]);
  }
  return result;
}

bool checksum_valid(const Bytes& frame, size_t offset, size_t length) {
  uint32_t sum = 0;
  for (size_t i = 0; i < length; i += 2) {
    sum += read_u16(frame, offset + i);
  }
  while (sum > 0xffff) {
    sum = (sum & 0xffff) + (sum >> 16);
  }
  return sum == 0xffff;
}

std::string join_flags(const std::vector<std::string>& names) {
  if (names.empty()) {
    return "none";
  }
  std::string result = names[0];
  for (size_t i = 1; i < names.size(); ++i) {
    result += "," + names[i];
  }
  return result;
}

bool parse_hex(const std::string& input, Bytes& frame) {
  std::string filtered;
  for (char ch : input) {
    if (std::isxdigit(static_cast<unsigned char>(ch))) {
      filtered.push_back(ch);
    }
  }
  if (filtered.size() % 2 != 0) {
    return false;
  }
  for (size_t i = 0; i < filtered.size(); i += 2) {
    frame.push_back(std::stoi(filtered.substr(i, 2), nullptr, 16));
  }
  return true;
}

void print(const std::string& key, const std::string& value) {
  std::cout << key << " " << value << "\n";
}

int main() {
  std::string line, input;
  while (std::getline(std::cin, line)) {
    input += line;
  }

  Bytes frame;
  if (!parse_hex(input, frame)) {
    std::cerr << "нечётное число шестнадцатеричных цифр\n";
    return EXIT_FAILURE;
  }

  const size_t eth_len = 14;
  if (frame.size() < eth_len) {
    std::cerr << "кадр короче заголовка Ethernet\n";
    return EXIT_FAILURE;
  }

  uint16_t ethertype = read_u16(frame, 12);
  print("eth.dst", mac(frame, 0));
  print("eth.src", mac(frame, 6));
  print("eth.ethertype", "0x" + hex_byte(ethertype >> 8) + hex_byte(ethertype & 0xff));

  if (ethertype != 0x0800) {
    return EXIT_SUCCESS;
  }

  const size_t ip = eth_len;
  if (frame.size() < ip + 20) {
    std::cerr << "пакет короче заголовка IPv4\n";
    return EXIT_FAILURE;
  }

  int version = frame[ip] >> 4;
  size_t ihl = (frame[ip] & 0x0f) * 4;
  uint16_t total_length = read_u16(frame, ip + 2);
  uint16_t id = read_u16(frame, ip + 4);
  uint16_t flags_and_offset = read_u16(frame, ip + 6);
  int ttl = frame[ip + 8];
  int protocol = frame[ip + 9];

  if (ihl < 20 || frame.size() < ip + ihl || total_length < ihl ||
      frame.size() < ip + total_length) {
    std::cerr << "некорректные длины в заголовке IPv4\n";
    return EXIT_FAILURE;
  }

  std::vector<std::string> ip_flags;
  if (flags_and_offset & 0x4000) {
    ip_flags.push_back("DF");
  }
  if (flags_and_offset & 0x2000) {
    ip_flags.push_back("MF");
  }

  print("ip.version", std::to_string(version));
  print("ip.ihl_bytes", std::to_string(ihl));
  print("ip.total_length", std::to_string(total_length));
  print("ip.id", "0x" + hex_byte(id >> 8) + hex_byte(id & 0xff));
  print("ip.flags", join_flags(ip_flags));
  print("ip.frag_offset", std::to_string((flags_and_offset & 0x1fff) * 8));
  print("ip.ttl", std::to_string(ttl));
  print("ip.protocol", std::to_string(protocol));
  print("ip.src", ipv4(frame, ip + 12));
  print("ip.dst", ipv4(frame, ip + 16));
  print("ip.checksum_valid", checksum_valid(frame, ip, ihl) ? "true" : "false");

  const size_t transport = ip + ihl;
  const size_t ip_end = ip + total_length;
  size_t header_len = 0;

  if (protocol == 6) {
    if (ip_end < transport + 20) {
      std::cerr << "сегмент короче заголовка TCP\n";
      return EXIT_FAILURE;
    }
    header_len = (frame[transport + 12] >> 4) * 4;
    uint8_t flags = frame[transport + 13];
    const char* names[] = {"FIN", "SYN", "RST", "PSH", "ACK", "URG"};
    std::vector<std::string> tcp_flags;
    for (int bit = 0; bit < 6; ++bit) {
      if (flags & (1 << bit)) {
        tcp_flags.push_back(names[bit]);
      }
    }

    print("tcp.src_port", std::to_string(read_u16(frame, transport)));
    print("tcp.dst_port", std::to_string(read_u16(frame, transport + 2)));
    print("tcp.seq", std::to_string(read_u32(frame, transport + 4)));
    print("tcp.ack", std::to_string(read_u32(frame, transport + 8)));
    print("tcp.data_offset_bytes", std::to_string(header_len));
    print("tcp.flags", join_flags(tcp_flags));
    print("tcp.window", std::to_string(read_u16(frame, transport + 14)));
  } else if (protocol == 17) {
    if (ip_end < transport + 8) {
      std::cerr << "дейтаграмма короче заголовка UDP\n";
      return EXIT_FAILURE;
    }
    header_len = 8;
    print("udp.src_port", std::to_string(read_u16(frame, transport)));
    print("udp.dst_port", std::to_string(read_u16(frame, transport + 2)));
    print("udp.length", std::to_string(read_u16(frame, transport + 4)));
  }

  if (ip_end < transport + header_len) {
    std::cerr << "заголовок транспортного уровня длиннее пакета\n";
    return EXIT_FAILURE;
  }
  print("payload.length", std::to_string(ip_end - transport - header_len));
  return EXIT_SUCCESS;
}
