#pragma once

#include <unordered_map>
#include <optional>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <vector>
#include <memory>
#include <string>

extern "C" {
  #include "lipon.h"
}

#include "lipon_dll.hpp"

#define LIPON_STACK_MAX (1024ULL * 8ULL)

#define EQUAL 0
#define BIGGER 1
#define SMALLER 2

namespace lipon {
  namespace debug {
    std::unordered_map<uint8_t, std::string> byte2string() {
      return { 
        {0x00, "push"}, {0x01, "add"}, {0x02, "sub"}, {0x03, "mul"},
        {0x04, "div"}, {0x05, "shl"}, {0x06, "shr"}, {0x07, "not"},
        {0x08, "xor"}, {0x09, "and"}, {0x0A, "or"}, {0x0B, "cmp"},
        {0x0C, "jmp"}, {0x0D, "je"}, {0x0E, "jg"}, {0x0F, "jl"},
        {0x10, "dup"}, {0x11, "call"}, {0x12, "ret"}, {0x13, "write"},
        {0x14, "read"}, {0x15, "call_c"}, {0x16, "halt"}, 
        {0x17, "pstr"}, {0x18, "rmstr"},
        {0x19, "addx"}, {0x1A, "subx"}, {0x1B, "mulx"}, {0x1C, "divx"},
        {0x1D, "modx"}, {0x1E, "mod"},
        {0x1F, "cvrtd"}, {0x20, "cvrtu"},
      };
    }
  }

  inline uint64_t read_u64_le(const uint8_t* p) {
    return
        (uint64_t)p[0] |
        ((uint64_t)p[1] << 8) |
        ((uint64_t)p[2] << 16) |
        ((uint64_t)p[3] << 24) |
        ((uint64_t)p[4] << 32) |
        ((uint64_t)p[5] << 40) |
        ((uint64_t)p[6] << 48) |
        ((uint64_t)p[7] << 56);
  }

  struct lipon_StringHold {
    std::unique_ptr<uint8_t[]> ptr;
    uint64_t length;
  };

  struct lipon_StringPoll {
    std::vector<std::optional<lipon_StringHold>> slots;
    std::vector<uint64_t> free_slots;
  };

  inline void lipon_Run(lipon_CState* state, const uint8_t* code, uint64_t* heap) {
    state->sp = 0;
    std::unique_ptr<uint64_t[]> unique_stack(new uint64_t[LIPON_STACK_MAX]);
    state->stack = unique_stack.get();
    state->heap = heap;
    std::vector<uint64_t> callstack;
    lipon_StringPoll stringpoll;
    state->stringpoll = &stringpoll;
    stringpoll.slots.reserve(0x20);
    stringpoll.free_slots.reserve(0x10);
    callstack.reserve(0x20);
    uint64_t ip = 0;

    state->push_string = [](void* data, const uint8_t* start, uint64_t length) -> uint64_t {
      auto stringpoll = (lipon_StringPoll*)data;

      lipon_StringHold hold;
      hold.ptr = std::make_unique<uint8_t[]>(length);
      hold.length = length;

      std::memcpy(hold.ptr.get(), start, length);
      uint64_t id = stringpoll->slots.size();
      if (!stringpoll->free_slots.empty()) {
        id = stringpoll->free_slots.back();
        stringpoll->free_slots.pop_back();
        stringpoll->slots[id] = std::move(hold);
      } else stringpoll->slots.emplace_back(std::move(hold));

      return id;
    };

    state->get_string = [](void* data, uint64_t idx) -> lipon_String {
      auto stringpoll = (lipon_StringPoll*)data;
      lipon_String str;
      str.front = (stringpoll)->slots[idx].value().ptr.get();
      str.length = (stringpoll)->slots[idx].value().length;
      return str;
    };

    state->remove_string = [](void* data, uint64_t idx) -> void {
      auto stringpoll = (lipon_StringPoll*)data;
      stringpoll->slots[idx] = std::nullopt;
      stringpoll->free_slots.push_back(idx);
    }; 

    static void* PRE_dispatch[] = {
      &&OP_PUSH, &&OP_ADD_INT, &&OP_SUB_INT, &&OP_MUL_INT, &&OP_DIV_INT,
      &&OP_SHL, &&OP_SHR, &&OP_NOT, &&OP_XOR, &&OP_AND, &&OP_OR,
      &&OP_CMP, &&OP_JMP, &&OP_JE, &&OP_JG, &&OP_JL,
      &&OP_DUP, &&OP_CALL, &&OP_RET, &&OP_WRITE, &&OP_READ,
      &&OP_CALL_C, &&OP_HALT, &&OP_PUSH_STR, &&OP_REMOVE_STR,
      &&OP_ADD_FLOAT, &&OP_SUB_FLOAT, &&OP_MUL_FLOAT, &&OP_DIV_FLOAT,
      &&OP_MOD_FLOAT, &&OP_MOD, &&OP_CAST_TO_DOUBLE, &&OP_CAST_TO_UINT
    };

    static void* dispatch[256];
    for (uint i = 0; i < sizeof(PRE_dispatch) / sizeof(void*); i++) {
      dispatch[i] = PRE_dispatch[i];
    } for (uint i = sizeof(PRE_dispatch) / sizeof(void*); i < 256; i++) {
      dispatch[i] = &&OP_ILLEGAL_INSTRUCTION;
    }

    #ifndef DEBUG
    #define DISPATCH() goto *dispatch[code[ip++]]
    #else
    auto byet2str = debug::byte2string();
    #define DISPATCH() \
      std::cout << "ip: " << ip << "\top: " << (int)code[ip] << "(" << byet2str[code[ip]] << ")" << std::endl; \
      goto *dispatch[code[ip++]];
    #endif

    DISPATCH();

    OP_PUSH: {
      LIPON_PUSH(state, read_u64_le(code + ip));
      ip += sizeof(uint64_t);
      DISPATCH();
    }
    OP_ADD_INT: {
      auto a = LIPON_POP(state), b = LIPON_POP(state);
      LIPON_PUSH(state, b + a);
      DISPATCH();
    }
    OP_SUB_INT: {
      auto a = LIPON_POP(state), b = LIPON_POP(state);
      LIPON_PUSH(state, b - a);
      DISPATCH();
    }
    OP_DIV_INT: {
      auto a = LIPON_POP(state), b = LIPON_POP(state);
      if (a == 0 || b == 0) throw std::runtime_error("division by zero");
      LIPON_PUSH(state, b / a);
      DISPATCH();
    }
    OP_MUL_INT: {
      auto a = LIPON_POP(state), b = LIPON_POP(state);
      LIPON_PUSH(state, b * a);
      DISPATCH();
    }
    OP_SHL: {
      auto a = LIPON_POP(state), b = LIPON_POP(state);
      LIPON_PUSH(state, b << a);
      DISPATCH();
    }
    OP_SHR: {
      auto a = LIPON_POP(state), b = LIPON_POP(state);
      LIPON_PUSH(state, b >> a);
      DISPATCH();
    }
    OP_NOT:
      LIPON_PUSH(state, ~LIPON_POP(state));
      DISPATCH();
    OP_XOR: {
      auto a = LIPON_POP(state), b = LIPON_POP(state);
      LIPON_PUSH(state, b ^ a);
      DISPATCH();
    }
    OP_AND: {
      auto a = LIPON_POP(state), b = LIPON_POP(state);
      LIPON_PUSH(state, b & a);
      DISPATCH();
    }
    OP_OR: {
      auto a = LIPON_POP(state), b = LIPON_POP(state);
      LIPON_PUSH(state, b | a);
      DISPATCH();
    }
    OP_CMP: {
      auto a = LIPON_POP(state), b = LIPON_POP(state);
      if (a > b) LIPON_PUSH(state, BIGGER);
      else if (a < b) LIPON_PUSH(state, SMALLER);
      else LIPON_PUSH(state, EQUAL);
      DISPATCH();
    }
    OP_JMP:
      ip = LIPON_POP(state);
      DISPATCH();
    OP_JE:
      if (LIPON_PEEK(state) == EQUAL) {
        LIPON_POP(state);
        ip = LIPON_POP(state);
      }
      DISPATCH();
    OP_JG:
      if (LIPON_PEEK(state) == BIGGER) {
        LIPON_POP(state);
        ip = LIPON_POP(state);
      }
      DISPATCH();
    OP_JL:
      if (LIPON_PEEK(state) == SMALLER) {
        LIPON_POP(state);
        ip = LIPON_POP(state);
      }
      DISPATCH();
    OP_DUP:
      LIPON_PUSH(state, LIPON_PEEK(state));
      DISPATCH();
    OP_CALL: {
      callstack.push_back(ip);
      ip = LIPON_POP(state);
      DISPATCH();
    }
    OP_RET:
      ip = callstack.back();
      callstack.pop_back();
      DISPATCH();
    OP_WRITE:
      state->heap[LIPON_POP(state)] = LIPON_POP(state);
      DISPATCH();
    OP_READ:
      LIPON_PUSH(state, state->heap[LIPON_POP(state)]);
      DISPATCH();
    OP_CALL_C: {
      state->cfunctions[LIPON_POP(state)](state);
      DISPATCH();
    }
    OP_PUSH_STR: {
      uint64_t len = read_u64_le(code + ip);
      ip += sizeof(uint64_t);

      uint64_t id = state->push_string(
        state->stringpoll,
        code + ip,
        len
      );

      ip += len;
      LIPON_PUSH(state, id);
      DISPATCH();
    }
    OP_REMOVE_STR: {
      state->remove_string(&stringpoll, LIPON_POP(state));
      DISPATCH();
    }
    OP_ADD_FLOAT: {
      auto a = std::bit_cast<double>(LIPON_POP(state)), b = std::bit_cast<double>(LIPON_POP(state));
      LIPON_PUSH(state, std::bit_cast<uint64_t>(b + a));
      DISPATCH();
    }
    OP_SUB_FLOAT: {
      auto a = std::bit_cast<double>(LIPON_POP(state)), b = std::bit_cast<double>(LIPON_POP(state));
      LIPON_PUSH(state, std::bit_cast<uint64_t>(b - a));
      DISPATCH();
    }
    OP_MUL_FLOAT: {
      auto a = std::bit_cast<double>(LIPON_POP(state)), b = std::bit_cast<double>(LIPON_POP(state));
      LIPON_PUSH(state, std::bit_cast<uint64_t>(b * a));
      DISPATCH();
    }
    OP_DIV_FLOAT: {
      auto a = std::bit_cast<double>(LIPON_POP(state)), b = std::bit_cast<double>(LIPON_POP(state));
      if (a == 0.0 || b == 0.0) throw std::runtime_error("division by zero");
      LIPON_PUSH(state, std::bit_cast<uint64_t>(b / a));
      DISPATCH();
    }
    OP_MOD_FLOAT: {
      auto a = std::bit_cast<double>(LIPON_POP(state)), b = std::bit_cast<double>(LIPON_POP(state));
      LIPON_PUSH(state, std::bit_cast<uint64_t>(std::fmod(b, a)));
      DISPATCH();
    }
    OP_MOD: {
      auto a = LIPON_POP(state), b = LIPON_POP(state);
      LIPON_PUSH(state, b % a);
      DISPATCH();
    }
    OP_CAST_TO_DOUBLE: {
      LIPON_PUSH(state, std::bit_cast<uint64_t>((double)LIPON_POP(state)));
      DISPATCH();
    }
    OP_CAST_TO_UINT: {
      LIPON_PUSH(state, (std::bit_cast<double>(LIPON_POP(state))));
      DISPATCH();
    }
    OP_HALT:
      return;
    OP_ILLEGAL_INSTRUCTION: {
      throw std::range_error("invalid range (instruction): illegal instructions. (Aborting execution)");
    }
  }

  class lipon_State {
  private:
    lipon_CState cstate;
    std::vector<lipon_DynamicLibrary> libraries;
    std::vector<lipon_Function> functions;
    
    inline uint64_t load_cfuncs(std::istream& input) {
      uint16_t lib_count;
      uint64_t offset = sizeof(uint16_t);
      input.read(reinterpret_cast<char*>(&lib_count), sizeof(lib_count));
      
      for (uint16_t i = 0; i < lib_count; ++i) {
        uint16_t len;
        input.read(reinterpret_cast<char*>(&len), sizeof(len));
        
        std::string path(len, '\0');
        input.read(path.data(), len);
  
        libraries.emplace_back(path);
        std::cout << "[v] loaded library " << path << std::endl;
  
        uint16_t func_count;
        input.read(reinterpret_cast<char*>(&func_count), sizeof(func_count));
        offset += sizeof(len) + len + sizeof(func_count);
  
        for (uint16_t f = 0; f < func_count; ++f) {
          uint16_t slen;
          input.read(reinterpret_cast<char*>(&slen), sizeof(slen));
  
          std::string symbol(slen, '\0');
          input.read(symbol.data(), slen);
          std::cout << "                   ├- " << symbol << std::flush;
  
          auto fn = libraries.back().get_function<lipon_Function>(symbol);
          if (!fn) {
            std::cout << ": err!" << std::endl;
            throw std::runtime_error("Failed loading symbol " + symbol);
          }
          std::cout << ": ok" << std::endl;
  
          functions.push_back(fn);
          offset += sizeof(slen) + slen;
        }
        std::cout << "                   └- " << func_count << " functions loaded" << std::endl;
      }
  
      cstate.cfunctions = functions.data();
      return offset;
    }

  public:
    inline lipon_State(size_t heap) {
      cstate.heap = new uint64_t[heap];
    }

    inline ~lipon_State() {
      delete[] cstate.heap;
    }

    inline void run(const std::vector<uint8_t>& code) {
      lipon_Run(&cstate, code.data(), cstate.heap);
    }

    inline void run(std::istream& src) {
    #ifndef DEBUG
    try {
    #endif // DEBUG
      std::cout << "lipon!" << std::endl;
      load_cfuncs(src);
      std::vector<uint8_t> code((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
      lipon_Run(&cstate, code.data(), cstate.heap);
    #ifndef DEBUG
      } catch (const std::exception& e) {
        std::cout << std::endl;
        std::cerr << "lipon: libc++ exception. what():\t" << e.what() << std::endl;
        std::cerr << "lipon: terminating." << std::endl;
        abort();
      }
    #endif // DEBUG
    }
  };
}