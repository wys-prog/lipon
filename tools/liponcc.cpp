#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "linet.hpp"

namespace fs = std::filesystem;

std::vector<std::string> required_files = {
    "cc",
    "cxx",
};

bool setup(const fs::path &basedir) {
  for (const auto &file : required_files) {
    if (!fs::is_regular_file(basedir / file)) {
      std::cerr << "[e]: " << __func__
                << ": missing required file: " << basedir / file << std::endl;
      return false;
    }
  }

  return true;
}

int main(int argc, char **argv) {
  if (argc < 1) {
    std::cerr << "[e]: " << __func__ << ": expected arguments! (run -help)"
              << std::endl;
    return EXIT_FAILURE;
  }

  fs::path basedir = argv[0];

  if (!setup(basedir)) {
    std::cerr << "[e]: " << __func__ << ": unable to init!" << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
/*
lipon cc hello.c -o hello
lipon install lipo.Std.Graphics
*/
