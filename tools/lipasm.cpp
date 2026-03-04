#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

static const std::unordered_map<std::string, uint8_t> OPCODES = {
    {"push", 0x00},  {"add", 0x01},    {"sub", 0x02},  {"mul", 0x03},
    {"div", 0x04},   {"shl", 0x05},    {"shr", 0x06},  {"not", 0x07},
    {"xor", 0x08},   {"and", 0x09},    {"or", 0x0A},   {"cmp", 0x0B},
    {"jmp", 0x0C},   {"je", 0x0D},     {"jg", 0x0E},   {"jl", 0x0F},
    {"dup", 0x10},   {"call", 0x11},   {"ret", 0x12},  {"write", 0x13},
    {"read", 0x14},  {"call_c", 0x15}, {"halt", 0x16}, {"pstr", 0x17},
    {"rmstr", 0x18}, {"addx", 0x19},   {"subx", 0x1A}, {"mulx", 0x1B},
    {"divx", 0x1C},  {"modx", 0x1D},   {"mod", 0x1E},  {"cvrtd", 0x1F},
    {"cvrtu", 0x20},
};

static const std::unordered_map<std::string, bool> INSTR_WITH_IMM = {
    {"push", true},
};

static std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  return s;
}

static std::string trim(const std::string &s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return "";
  size_t end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

static std::string stripUnderscores(std::string s) {
  s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
  return s;
}

static int64_t parseIntToken(const std::string &raw) {
  std::string t = stripUnderscores(trim(raw));
  if (t.empty())
    throw std::invalid_argument("Empty integer token");
  return (int64_t)std::stoull(t, nullptr, 0);
}

static std::vector<uint8_t> unescapeString(const std::string &s) {
  std::vector<uint8_t> result;
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char nxt = s[i + 1];
      static const std::unordered_map<char, uint8_t> esc = {
          {'n', 10},  {'t', 9},  {'r', 13}, {'0', 0},
          {'\\', 92}, {'"', 34}, {'\'', 39}};
      auto it = esc.find(nxt);
      if (it != esc.end()) {
        result.push_back(it->second);
        i += 2;
      } else if (nxt == 'x' && i + 3 < s.size()) {
        std::string hex = s.substr(i + 2, 2);
        result.push_back((uint8_t)std::stoul(hex, nullptr, 16));
        i += 4;
      } else {
        result.push_back((uint8_t)s[i]);
        i++;
      }
    } else {
      result.push_back((uint8_t)s[i]);
      i++;
    }
  }
  return result;
}

struct LibEntry {
  std::string path;
  std::vector<std::string> funcs;
};

struct Patch {
  size_t offset;
  std::string label;
  int lineno;
};

class Assembler {
public:
  std::string source;
  std::string sourceName;
  std::vector<LibEntry> libs;
  std::unordered_map<std::string, int> funcIndex;
  std::vector<uint8_t> code;
  std::unordered_map<std::string, uint64_t> labels;
  std::vector<Patch> patches;

  Assembler(const std::string &src, const std::string &name = "<stdin>")
      : source(src), sourceName(name) {}

  void emitByte(uint8_t b) { code.push_back(b); }

  void emitU16(uint16_t v) {
    code.push_back(v & 0xFF);
    code.push_back((v >> 8) & 0xFF);
  }

  void emitU32(uint32_t v) {
    for (int i = 0; i < 4; i++)
      code.push_back((v >> (8 * i)) & 0xFF);
  }

  void emitU64(uint64_t v) {
    for (int i = 0; i < 8; i++)
      code.push_back((v >> (8 * i)) & 0xFF);
  }

  size_t currentOffset() { return code.size(); }

  void reserveU64Patch(const std::string &label, int lineno) {
    size_t off = currentOffset();
    emitU64(0xDEADBEEFDEADBEEFULL);
    patches.push_back({off, label, lineno});
  }

  static std::vector<std::string> tokeniseLine(const std::string &line) {
    std::vector<std::string> tokens;
    std::string current;
    bool inString = false;
    for (size_t i = 0; i < line.size(); i++) {
      char ch = line[i];
      if (ch == ';' && !inString)
        break;
      if (ch == '"') {
        if (inString) {
          current += ch;
          tokens.push_back(current);
          current = "";
          inString = false;
        } else {
          std::string t = trim(current);
          if (!t.empty())
            tokens.push_back(t);
          current = std::string(1, ch);
          inString = true;
        }
      } else if ((ch == ' ' || ch == '\t' || ch == ',') && !inString) {
        std::string t = trim(current);
        if (!t.empty()) {
          tokens.push_back(t);
          current = "";
        }
      } else {
        current += ch;
      }
    }
    std::string t = trim(current);
    if (!t.empty())
      tokens.push_back(t);
    return tokens;
  }

  using NumberedLines = std::vector<std::pair<int, std::string>>;

  NumberedLines parseImports(const NumberedLines &lines) {
    NumberedLines remaining;
    int globalFuncIdx = 0;
    LibEntry *currentLib = nullptr;

    for (size_t i = 0; i < lines.size(); i++) {
      int lineno = lines[i].first;
      const std::string &raw = lines[i].second;
      auto tokens = tokeniseLine(raw);

      if (tokens.empty()) {
        remaining.push_back(lines[i]);
        continue;
      }

      std::string directive = toLower(tokens[0]);

      if (directive == ".import") {
        if (tokens.size() < 2 || tokens[1].front() != '"')
          throw std::runtime_error("Line " + std::to_string(lineno) +
                                   ": .import expects a quoted library path");
        std::string path = tokens[1].substr(1, tokens[1].size() - 2);
        libs.push_back({path, {}});
        currentLib = &libs.back();
        continue;
      }

      if (directive == "func" && currentLib) {
        if (tokens.size() < 2)
          throw std::runtime_error("Line " + std::to_string(lineno) +
                                   ": func expects a symbol name");
        std::string sym = tokens[1];
        currentLib->funcs.push_back(sym);
        if (funcIndex.find(sym) == funcIndex.end())
          funcIndex[sym] = globalFuncIdx++;
        continue;
      }

      currentLib = nullptr;
      remaining.push_back(lines[i]);
    }
    return remaining;
  }

  static bool isNumericLiteral(const std::string &s) {
    static const std::regex numRe(
        R"(^-?(?:0x[0-9a-fA-F_]+|0b[01_]+|0o[0-7_]+|[0-9_]+)$)");
    return std::regex_match(s, numRe);
  }

  void assembleBody(const NumberedLines &lines) {
    for (auto &[lineno, raw] : lines) {
      auto tokens = tokeniseLine(raw);
      if (tokens.empty())
        continue;

      size_t ti = 0;
      std::string first = tokens[ti];

      if (first.back() == ':') {
        std::string label = first.substr(0, first.size() - 1);
        if (label.empty())
          throw std::runtime_error("Line " + std::to_string(lineno) +
                                   ": empty label name");
        labels[label] = currentOffset();
        ti++;
        if (ti >= tokens.size())
          continue;
        first = tokens[ti];
      }

      std::string directive = toLower(first);

      auto emitDataBytes = [&](const std::vector<uint8_t> &data) {
        code.insert(code.end(), data.begin(), data.end());
      };

      if (directive == ".string") {
        if (tokens.size() < ti + 2 || tokens[ti + 1].front() != '"')
          throw std::runtime_error("Line " + std::to_string(lineno) +
                                   ": .string expects a quoted value");
        std::string raw_str =
            tokens[ti + 1].substr(1, tokens[ti + 1].size() - 2);
        auto data = unescapeString(raw_str);
        data.push_back(0);
        emitDataBytes(data);
        continue;
      }

      if (directive == ".lstr") {
        if (tokens.size() < ti + 2 || tokens[ti + 1].front() != '"')
          throw std::runtime_error("Line " + std::to_string(lineno) +
                                   ": .lstr expects a quoted value");
        std::string raw_str =
            tokens[ti + 1].substr(1, tokens[ti + 1].size() - 2);
        auto data = unescapeString(raw_str);
        emitU64((uint64_t)data.size());
        emitDataBytes(data);
        continue;
      }

      if (directive == ".byte") {
        if (tokens.size() <= ti + 1)
          throw std::runtime_error("Line " + std::to_string(lineno) +
                                   ": .byte expects at least one value");
        for (size_t j = ti + 1; j < tokens.size(); j++) {
          int64_t v = parseIntToken(tokens[j]);
          if (v < 0 || v > 0xFF)
            throw std::runtime_error("Line " + std::to_string(lineno) +
                                     ": .byte out of range");
          emitByte((uint8_t)v);
        }
        continue;
      }

      if (directive == ".word") {
        if (tokens.size() <= ti + 1)
          throw std::runtime_error("Line " + std::to_string(lineno) +
                                   ": .word expects at least one value");
        for (size_t j = ti + 1; j < tokens.size(); j++)
          emitU16((uint16_t)parseIntToken(tokens[j]));
        continue;
      }

      if (directive == ".dword") {
        if (tokens.size() <= ti + 1)
          throw std::runtime_error("Line " + std::to_string(lineno) +
                                   ": .dword expects at least one value");
        for (size_t j = ti + 1; j < tokens.size(); j++)
          emitU32((uint32_t)parseIntToken(tokens[j]));
        continue;
      }

      if (directive == ".qword") {
        if (tokens.size() <= ti + 1)
          throw std::runtime_error("Line " + std::to_string(lineno) +
                                   ": .qword expects at least one value");
        for (size_t j = ti + 1; j < tokens.size(); j++)
          emitU64((uint64_t)parseIntToken(tokens[j]));
        continue;
      }

      if (directive == ".array") {
        if (tokens.size() < ti + 3)
          throw std::runtime_error("Line " + std::to_string(lineno) +
                                   ": .array <count> <value>");
        int64_t count = parseIntToken(tokens[ti + 1]);
        uint64_t value = (uint64_t)parseIntToken(tokens[ti + 2]);
        for (int64_t k = 0; k < count; k++)
          emitU64(value);
        continue;
      }

      std::string mnemonic = directive;
      auto opIt = OPCODES.find(mnemonic);
      if (opIt == OPCODES.end())
        throw std::runtime_error("Line " + std::to_string(lineno) +
                                 ": unknown mnemonic or directive '" + first +
                                 "'");

      emitByte(opIt->second);

      if (INSTR_WITH_IMM.count(mnemonic)) {
        if (tokens.size() < ti + 2)
          throw std::runtime_error("Line " + std::to_string(lineno) + ": '" +
                                   mnemonic + "' requires an operand");
        std::string operand = tokens[ti + 1];

        if (mnemonic == "call_c" && funcIndex.count(operand)) {
          emitU64((uint64_t)funcIndex[operand]);
        } else if (isNumericLiteral(operand)) {
          emitU64((uint64_t)parseIntToken(operand));
        } else {
          auto lit = labels.find(operand);
          if (lit != labels.end()) {
            emitU64(lit->second);
          } else {
            reserveU64Patch(operand, lineno);
          }
        }
      }
    }
  }

  void resolvePatches() {
    int errors = 0;
    for (auto &p : patches) {
      auto it = labels.find(p.label);
      if (it == labels.end()) {
        std::cerr << "Error — Line " << p.lineno << ": undefined symbol '"
                  << p.label << "'\n";
        errors++;
        continue;
      }
      uint64_t addr = it->second;
      memcpy(code.data() + p.offset, &addr, 8);
    }
    if (errors)
      throw std::runtime_error(std::to_string(errors) +
                               " unresolved symbol(s). Assembly aborted.");
  }

  std::vector<uint8_t> buildImportHeader() {
    std::vector<uint8_t> header;
    auto appendU16 = [&](uint16_t v) {
      header.push_back(v & 0xFF);
      header.push_back((v >> 8) & 0xFF);
    };
    appendU16((uint16_t)libs.size());
    for (auto &lib : libs) {
      auto pb = std::vector<uint8_t>(lib.path.begin(), lib.path.end());
      appendU16((uint16_t)pb.size());
      header.insert(header.end(), pb.begin(), pb.end());
      appendU16((uint16_t)lib.funcs.size());
      for (auto &sym : lib.funcs) {
        auto sb = std::vector<uint8_t>(sym.begin(), sym.end());
        appendU16((uint16_t)sb.size());
        header.insert(header.end(), sb.begin(), sb.end());
      }
    }
    return header;
  }

  std::vector<uint8_t> assemble() {
    NumberedLines numbered;
    std::istringstream ss(source);
    std::string line;
    int i = 1;
    while (std::getline(ss, line)) {
      numbered.push_back({i++, line});
    }

    auto remaining = parseImports(numbered);
    assembleBody(remaining);
    resolvePatches();

    auto header = buildImportHeader();
    std::vector<uint8_t> result;
    result.insert(result.end(), header.begin(), header.end());
    result.insert(result.end(), code.begin(), code.end());
    return result;
  }
};

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <input.asm> <output.lipo>\n";
    return 1;
  }

  std::filesystem::path inPath = argv[1];
  std::filesystem::path outPath = argv[2];

  if (!std::filesystem::exists(inPath)) {
    std::cerr << "Error: input file not found: " << inPath << "\n";
    return 1;
  }

  std::ifstream inFile(inPath, std::ios::binary);
  std::string source((std::istreambuf_iterator<char>(inFile)),
                     std::istreambuf_iterator<char>());

  try {
    Assembler asm_(source, inPath.string());
    auto binary = asm_.assemble();
    std::ofstream outFile(outPath, std::ios::binary);
    outFile.write((const char *)binary.data(), binary.size());
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
