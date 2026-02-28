#!/usr/bin/env python3

import sys
import struct
import re
from pathlib import Path
from typing import Optional

OPCODES = {
    "push":    0x00,
    "add":     0x01,
    "sub":     0x02,
    "mul":     0x03,
    "div":     0x04,
    "shl":     0x05,
    "shr":     0x06,
    "not":     0x07,
    "xor":     0x08,
    "and":     0x09,
    "or":      0x0A,
    "cmp":     0x0B,
    "jmp":     0x0C,
    "je":      0x0D,
    "jg":      0x0E,
    "jl":      0x0F,
    "dup":     0x10,
    "call":    0x11,
    "ret":     0x12,
    "write":   0x13,
    "read":    0x14,
    "call_c":  0x15,
    "halt":    0x16,
    "pstr":    0x17,
    "rmstr":   0x18,
    "addx":    0x19,
    "subx":    0x1A,
    "mulx":    0x1B,
    "divx":    0x1C,
    "modx":    0x1D,
    "mod":     0x1E,
    "cvrtd":   0x1F,
    "cvrtu":   0x20,
}

INSTR_WITH_IMM = {"push"}

def parse_int(token: str) -> int:
    token = token.strip().replace("_", "")
    return int(token, 0)

def unescape_string(s: str) -> bytes:
    result = bytearray()
    i = 0
    while i < len(s):
        if s[i] == '\\' and i + 1 < len(s):
            nxt = s[i+1]
            esc = {'n': 10, 't': 9, 'r': 13, '0': 0, '\\': 92, '"': 34, "'": 39}
            if nxt in esc:
                result.append(esc[nxt])
                i += 2
            elif nxt == 'x' and i + 3 < len(s):
                result.append(int(s[i+2:i+4], 16))
                i += 4
            else:
                result.append(ord(s[i]))
                i += 1
        else:
            result.extend(s[i].encode('utf-8'))
            i += 1
    return bytes(result)

class Assembler:
    def __init__(self, source: str, source_name: str = "<stdin>"):
        self.source      = source
        self.source_name = source_name
        self.libs: list[dict[str, str]] = []
        self.func_index: dict[str, int] = {}
        self.code: bytearray = bytearray()
        self.labels: dict[str, int] = {}
        self.patches: list[tuple[int, str, int]] = []
        self.stat_instructions = 0
        self.stat_data_bytes   = 0
        self.stat_labels       = 0

    def _emit_byte(self, b: int):
        self.code.append(b & 0xFF)

    def _emit_u16(self, v: int):
        self.code += struct.pack("<H", v & 0xFFFF)

    def _emit_u32(self, v: int):
        self.code += struct.pack("<I", v & 0xFFFFFFFF)

    def _emit_u64(self, v: int):
        self.code += struct.pack("<Q", v & 0xFFFFFFFFFFFFFFFF)

    def _current_offset(self) -> int:
        return len(self.code)

    def _reserve_u64_patch(self, label: str, lineno: int) -> int:
        offset = self._current_offset()
        self._emit_u64(0xDEADBEEFDEADBEEF)
        self.patches.append((offset, label, lineno))
        return offset

    @staticmethod
    def tokenise_line(line: str) -> list[str]:
        tokens: list[str] = []
        current = ""
        in_string = False
        i = 0
        while i < len(line):
            ch = line[i]
            if ch == ';' and not in_string:
                break
            if ch == '"':
                if in_string:
                    current += ch
                    tokens.append(current)
                    current = ""
                    in_string = False
                else:
                    if current.strip():
                        tokens.append(current.strip())
                    current = ch
                    in_string = True
            elif ch in (' ', '\t', ',') and not in_string:
                if current.strip():
                    tokens.append(current.strip())
                    current = ""
            else:
                current += ch
            i += 1
        if current.strip():
            tokens.append(current.strip())
        return tokens

    def _parse_imports(self, lines: list[tuple[int, str]]) -> list[tuple[int, str]]:
        remaining: list[tuple[int, str]] = []
        i = 0
        current_lib: Optional[dict[object|str, object|str]] = None
        global_func_idx = 0

        while i < len(lines):
            lineno, raw = lines[i]
            tokens = self.tokenise_line(raw)

            if not tokens:
                i += 1
                remaining.append(lines[i-1])
                continue

            directive = tokens[0].lower()

            if directive == ".import":
                if len(tokens) < 2 or not tokens[1].startswith('"'):
                    raise SyntaxError(f"Line {lineno}: .import expects a quoted library path")
                path = tokens[1][1:-1]
                current_lib = {"path": path, "funcs": []}
                self.libs.append(current_lib)
                i += 1
                continue

            if directive == "func" and current_lib is not None:
                if len(tokens) < 2:
                    raise SyntaxError(f"Line {lineno}: func expects a symbol name")
                symbol = tokens[1]
                current_lib["funcs"].append(symbol)
                if symbol not in self.func_index:
                    self.func_index[symbol] = global_func_idx
                    global_func_idx += 1
                i += 1
                continue

            current_lib = None
            remaining.append(lines[i])
            i += 1

        return remaining

    def _assemble_body(self, lines: list[tuple[int, str]]):
        for lineno, raw in lines:
            tokens = self.tokenise_line(raw)
            if not tokens:
                continue

            first = tokens[0]

            if first.endswith(':'):
                label = first[:-1]
                if not label:
                    raise SyntaxError(f"Line {lineno}: empty label name")
                addr = self._current_offset()
                self.labels[label] = addr
                self.stat_labels += 1
                tokens = tokens[1:]
                if not tokens:
                    continue
                first = tokens[0]

            directive = first.lower()

            if directive == ".string":
                if len(tokens) < 2 or not tokens[1].startswith('"'):
                    raise SyntaxError(f"Line {lineno}: .string expects a quoted value")
                raw_str = tokens[1][1:-1]
                data = unescape_string(raw_str) + b'\x00'
                self.code += data
                self.stat_data_bytes += len(data)
                continue

            if directive == ".lstr":
                if len(tokens) < 2 or not tokens[1].startswith('"'):
                    raise SyntaxError(f"Line {lineno}: .lstr expects a quoted value")
                raw_str = tokens[1][1:-1]
                data = unescape_string(raw_str)
                self._emit_u64(len(data))
                self.code += data
                self.stat_data_bytes += len(data)
                continue

            if directive == ".byte":
                values = tokens[1:]
                if not values:
                    raise SyntaxError(f"Line {lineno}: .byte expects at least one value")
                for v in values:
                    b = parse_int(v)
                    if not (0 <= b <= 0xFF):
                        raise ValueError(f"Line {lineno}: .byte value out of range: {v}")
                    self._emit_byte(b)
                self.stat_data_bytes += len(values)
                continue

            if directive == ".word":
                values = tokens[1:]
                if not values:
                    raise SyntaxError(f"Line {lineno}: .word expects at least one value")
                for v in values:
                    self._emit_u16(parse_int(v))
                self.stat_data_bytes += len(values) * 2
                continue

            if directive == ".dword":
                values = tokens[1:]
                if not values:
                    raise SyntaxError(f"Line {lineno}: .dword expects at least one value")
                for v in values:
                    self._emit_u32(parse_int(v))
                self.stat_data_bytes += len(values) * 4
                continue

            if directive == ".qword":
                values = tokens[1:]
                if not values:
                    raise SyntaxError(f"Line {lineno}: .qword expects at least one value")
                for v in values:
                    self._emit_u64(parse_int(v))
                self.stat_data_bytes += len(values) * 8
                continue

            if directive == ".array":
                if len(tokens) < 3:
                    raise SyntaxError(f"Line {lineno}: .array <count> <value>")
                count = parse_int(tokens[1])
                value = parse_int(tokens[2])
                for _ in range(count):
                    self._emit_u64(value)
                self.stat_data_bytes += count * 8
                continue

            mnemonic = directive
            if mnemonic not in OPCODES:
                raise SyntaxError(f"Line {lineno}: unknown mnemonic or directive '{first}'")

            opcode = OPCODES[mnemonic]
            self._emit_byte(opcode)
            self.stat_instructions += 1

            if mnemonic in INSTR_WITH_IMM:
                if len(tokens) < 2:
                    raise SyntaxError(f"Line {lineno}: '{mnemonic}' requires an operand")
                operand = tokens[1]

                if mnemonic == "call_c" and operand in self.func_index:
                    self._emit_u64(self.func_index[operand])
                elif re.match(r'^-?(?:0x[0-9a-fA-F_]+|0b[01_]+|0o[0-7_]+|[0-9_]+)$', operand):
                    val = parse_int(operand)
                    self._emit_u64(val)
                else:
                    if operand in self.labels:
                        self._emit_u64(self.labels[operand])
                    else:
                        self._reserve_u64_patch(operand, lineno)

    def _resolve_patches(self):
        errors = 0
        for offset, label, lineno in self.patches:
            if label not in self.labels:
                print(f"Error — Line {lineno}: undefined label '{label}'")
                errors += 1
                continue
            addr = self.labels[label]
            struct.pack_into("<Q", self.code, offset, addr)
        if errors:
            raise RuntimeError(f"{errors} unresolved label(s). Assembly aborted.")

    def _build_import_header(self) -> bytes:
        header = bytearray()
        header += struct.pack("<H", len(self.libs))
        for lib in self.libs:
            path_b = lib["path"].encode("utf-8")
            header += struct.pack("<H", len(path_b))
            header += path_b
            header += struct.pack("<H", len(lib["funcs"]))
            for sym in lib["funcs"]:
                sym_b = sym.encode("utf-8")
                header += struct.pack("<H", len(sym_b))
                header += sym_b
        return bytes(header)

    def assemble(self) -> bytes:
        raw_lines = self.source.splitlines()
        numbered = [(i + 1, line) for i, line in enumerate(raw_lines)]
        remaining = self._parse_imports(numbered)
        self._assemble_body(remaining)
        self._resolve_patches()
        header = self._build_import_header()
        return header + bytes(self.code)


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <input.asm> <output.lipo>")
        sys.exit(1)

    in_path  = Path(sys.argv[1])
    out_path = Path(sys.argv[2])

    if not in_path.exists():
        print(f"Error: input file not found: {in_path}")
        sys.exit(1)

    source = in_path.read_text(encoding="utf-8")

    try:
        asm    = Assembler(source, str(in_path))
        binary = asm.assemble()
        out_path.write_bytes(binary)
    except (SyntaxError, ValueError, RuntimeError) as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()