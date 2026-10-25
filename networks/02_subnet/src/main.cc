#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

bool parse_number(const std::string& text, int max, int& value) {
  if (text.empty() || text.size() > 3) {
    return false;
  }
  for (char ch : text) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
  }
  value = std::stoi(text);
  return value <= max;
}

bool parse_ipv4(const std::string& text, uint32_t& address) {
  std::stringstream stream(text);
  std::string part;
  address = 0;
  int count = 0;
  while (std::getline(stream, part, '.')) {
    int octet;
    if (count == 4 || !parse_number(part, 255, octet)) {
      return false;
    }
    address = (address << 8) | octet;
    ++count;
  }
  return count == 4 && text.back() != '.';
}

bool parse_cidr(const std::string& text, uint32_t& address, int& prefix) {
  size_t slash = text.find('/');
  if (slash == std::string::npos) {
    return false;
  }
  return parse_ipv4(text.substr(0, slash), address) &&
         parse_number(text.substr(slash + 1), 32, prefix);
}

uint32_t mask_for(int prefix) {
  if (prefix == 0) {
    return 0;
  }
  return 0xffffffffu << (32 - prefix);
}

std::string format_ipv4(uint32_t address) {
  return std::to_string(address >> 24) + "." + std::to_string((address >> 16) & 0xff) + "." +
         std::to_string((address >> 8) & 0xff) + "." + std::to_string(address & 0xff);
}

void print(const std::string& key, const std::string& value) {
  std::cout << key << " " << value << "\n";
}

int run_subnet(const std::string& spec) {
  uint32_t address;
  int prefix;
  if (!parse_cidr(spec, address, prefix)) {
    std::cerr << "неверный блок адресов: " << spec << "\n";
    return EXIT_FAILURE;
  }

  uint32_t mask = mask_for(prefix);
  uint32_t network = address & mask;
  uint32_t last_address = network | ~mask;

  print("network", format_ipv4(network));
  print("netmask", format_ipv4(mask));
  print("prefix", std::to_string(prefix));

  if (prefix == 32) {
    print("broadcast", "none");
    print("first", format_ipv4(network));
    print("last", format_ipv4(network));
    print("hosts", "1");
  } else if (prefix == 31) {
    print("broadcast", "none");
    print("first", format_ipv4(network));
    print("last", format_ipv4(last_address));
    print("hosts", "2");
  } else {
    uint64_t block_size = uint64_t(1) << (32 - prefix);
    print("broadcast", format_ipv4(last_address));
    print("first", format_ipv4(network + 1));
    print("last", format_ipv4(last_address - 1));
    print("hosts", std::to_string(block_size - 2));
  }
  return EXIT_SUCCESS;
}

int run_route(const std::string& table_path, const std::string& destination_text) {
  uint32_t destination;
  if (!parse_ipv4(destination_text, destination)) {
    std::cerr << "неверный адрес назначения: " << destination_text << "\n";
    return EXIT_FAILURE;
  }

  std::ifstream table(table_path);
  if (!table) {
    std::cerr << "не удалось открыть таблицу: " << table_path << "\n";
    return EXIT_FAILURE;
  }

  int best_prefix = -1;
  std::string best_interface;
  std::string line;
  while (std::getline(table, line)) {
    std::stringstream stream(line);
    std::string spec, interface;
    if (!(stream >> spec) || spec[0] == '#') {
      continue;
    }
    uint32_t network;
    int prefix;
    if (!(stream >> interface) || !parse_cidr(spec, network, prefix)) {
      std::cerr << "неверная запись таблицы: " << line << "\n";
      return EXIT_FAILURE;
    }
    uint32_t mask = mask_for(prefix);
    if ((destination & mask) == (network & mask) && prefix > best_prefix) {
      best_prefix = prefix;
      best_interface = interface;
    }
  }

  if (best_prefix < 0) {
    print("unreachable", "true");
    return EXIT_FAILURE;
  }
  print("via", best_interface);
  print("prefix", std::to_string(best_prefix));
  return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
  std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "subnet" && argc == 3) {
    return run_subnet(argv[2]);
  }
  if (mode == "route" && argc == 4) {
    return run_route(argv[2], argv[3]);
  }
  std::cerr << "использование:\n  " << argv[0] << " subnet <адрес>/<префикс>\n  " << argv[0]
            << " route <файл таблицы> <адрес назначения>\n";
  return EXIT_FAILURE;
}
