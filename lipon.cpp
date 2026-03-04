#include "lipon.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <file>" << std::endl;
    return 1;
  }

  std::ifstream file(argv[1]);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file " << argv[1] << std::endl;
    return 1;
  }

  int heap_size = 0x4000;

  for (int i = 2; i < argc; i++) {
    if (std::string(argv[i]) == "-hm" && i + 1 < argc) {
      heap_size = std::stoi(argv[i + 1], nullptr, 16);
      i++;
    }
  }
  lipon::lipon_State state(heap_size);
  state.run(file);
  file.close();

  return 0;
}
