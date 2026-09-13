#!/usr/bin/env python3
"""FunAIGameEngine 游戏脚本编译器: game.gs -> 字节码。

纯标准库。字节码规范见 docs/SCRIPT.md。

  py -3 tools\\gs_compiler.py game.gs                # 报错检查 + 统计
  py -3 tools\\gs_compiler.py game.gs --listing      # 反汇编
  py -3 tools\\gs_compiler.py game.gs -o out.bin     # 只导出字节码(调试用)
"""
import argparse
import struct
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# 字节码定义(必须与 engine/src/ScriptGame.cpp 保持一致)
# ---------------------------------------------------------------------------
OP_HALT, OP_PUSH, OP_LOADG, OP_STOREG, OP_POP = 0x00, 0x01, 0x02, 0x03, 0x04
OP_LDMEM, OP_STMEM, OP_LDIDX, OP_STIDX = 0x05, 0x06, 0x07, 0x08
OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD = 0x10, 0x11, 0x12, 0x13, 0x14
OP_NEG, OP_LNOT = 0x15, 0x16
OP_LT, OP_LE, OP_GT, OP_GE, OP_EQ, OP_NE = 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C
OP_LAND, OP_LOR = 0x1D, 0x1E
OP_AND = 0x1F
OP_JMP, OP_JZ, OP_JNZ = 0x20, 0x21, 0x22
OP_OR, OP_XOR, OP_SHL, OP_SHR = 0x23, 0x24, 0x25, 0x26
OP_CALLN = 0x30
OP_CALL_USER = 0x31           # 调用脚本自己定义的函数(地址是 u16)
OP_RET = 0x32                 # 从函数返回(VM 里有独立的返回地址栈)
OP_IMG, OP_TEXT, OP_SFX, OP_BGM = 0x40, 0x41, 0x42, 0x43
OP_IMGF, OP_IMGV, OP_IMGW, OP_IMGH = 0x44, 0x45, 0x46, 0x47
OP_SCHR = 0x48                # chr(strs[i], pos): 弹 pos, idx -> 压字符码
OP_FADD, OP_FSUB, OP_FMUL, OP_FDIV, OP_FNEG = 0x50, 0x51, 0x52, 0x53, 0x54
OP_FLT, OP_FLE, OP_FGT, OP_FGE, OP_FEQ, OP_FNE = 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A
OP_ITOF, OP_FTOI, OP_FLAND, OP_FLOR, OP_FLNOT = 0x5B, 0x5C, 0x5D, 0x5E, 0x5F

OP_NAMES = {
    OP_HALT: "HALT", OP_PUSH: "PUSH", OP_LOADG: "LOADG", OP_STOREG: "STOREG", OP_POP: "POP",
    OP_LDMEM: "LDMEM", OP_STMEM: "STMEM", OP_LDIDX: "LDIDX", OP_STIDX: "STIDX",
    OP_ADD: "ADD", OP_SUB: "SUB", OP_MUL: "MUL", OP_DIV: "DIV", OP_MOD: "MOD",
    OP_NEG: "NEG", OP_LNOT: "LNOT", OP_LT: "LT", OP_LE: "LE", OP_GT: "GT", OP_GE: "GE",
    OP_EQ: "EQ", OP_NE: "NE", OP_LAND: "LAND", OP_LOR: "LOR", OP_AND: "AND",
    OP_JMP: "JMP", OP_JZ: "JZ", OP_JNZ: "JNZ",
    OP_OR: "OR", OP_XOR: "XOR", OP_SHL: "SHL", OP_SHR: "SHR", OP_CALLN: "CALLN",
    OP_CALL_USER: "CALL", OP_RET: "RET",
    OP_IMG: "IMG", OP_TEXT: "TEXT", OP_SFX: "SFX", OP_BGM: "BGM",
    OP_IMGF: "IMGF", OP_IMGV: "IMGV", OP_IMGW: "IMGW", OP_IMGH: "IMGH", OP_SCHR: "SCHR",
    OP_FADD: "FADD", OP_FSUB: "FSUB", OP_FMUL: "FMUL", OP_FDIV: "FDIV", OP_FNEG: "FNEG",
    OP_FLT: "FLT", OP_FLE: "FLE", OP_FGT: "FGT", OP_FGE: "FGE", OP_FEQ: "FEQ",
    OP_FNE: "FNE", OP_ITOF: "ITOF", OP_FTOI: "FTOI", OP_FLAND: "FLAND",
    OP_FLOR: "FLOR", OP_FLNOT: "FLNOT",
}

# 类型: 脚本只有两种数值类型(int / float32), 类型完全由编译器静态推导。
T_I, T_F = "i", "f"

# 数值内置函数: 名字 -> (id, 参数个数, 返回类型, [参数类型])
#   参数类型只写 [T_F] 时表示那几个参数要浮点, 其余强制转 int
NUM_BUILTINS = {
    "held": (0, 1, T_I, [T_I]), "pressed": (1, 1, T_I, [T_I]),
    "released": (2, 1, T_I, [T_I]),
    "rnd": (3, 1, T_I, [T_I]), "rgb": (4, 3, T_I, [T_I] * 3),
    "dt_ms": (5, 0, T_I, []), "frame": (6, 0, T_I, []), "exit": (7, 0, T_I, []),
    "clear": (8, 1, T_I, [T_I]), "px": (9, 3, T_I, [T_I] * 3),
    "rect": (10, 5, T_I, [T_I] * 5), "frect": (11, 5, T_I, [T_I] * 5),
    "screen_w": (12, 0, T_I, []), "screen_h": (13, 0, T_I, []),
    "textnum": (14, 5, T_I, [T_I] * 5),
    "abs": (15, 1, T_I, [T_I]), "imin": (16, 2, T_I, [T_I, T_I]),
    "imax": (17, 2, T_I, [T_I, T_I]),
    "itof": (18, 1, T_F, [T_I]), "ftoi": (19, 1, T_I, [T_F]),
    "fabs": (20, 1, T_F, [T_F]), "dtf": (21, 0, T_F, []),
    "fmin": (22, 2, T_F, [T_F, T_F]), "fmax": (23, 2, T_F, [T_F, T_F]),
    "fsqrt": (24, 1, T_F, [T_F]),
}
# 字符串内置函数(无返回值, 只能当语句): 名字 -> (opcode, 名字外的参数个数)
STR_BUILTINS = {"img": (OP_IMG, 2), "text": (OP_TEXT, 4), "sfx": (OP_SFX, 0), "bgm": (OP_BGM, 0)}
# 带字符串参数、有返回值的: 名字 -> opcode
STRNUM_BUILTINS = {"imgw": OP_IMGW, "imgh": OP_IMGH}

BUTTONS = {"up": 0, "down": 1, "left": 2, "right": 3, "a": 4, "b": 5, "c": 6, "d": 7,
           "start": 8, "select": 9, "l": 10, "r": 11, "x": 12, "y": 13}

MAX_GLOBALS = 2048          # 标量 + 数组元素的总量(对应 VM 的 globals_ 内存池)
MAX_SECTIONS = ("start", "update", "render")

ASSIGN_OPS = ("=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=", "^=")
COMPOUND = {"+=": "+", "-=": "-", "*=": "*", "/=": "/", "%=": "%",
            "<<=": "<<", ">>=": ">>", "&=": "&", "|=": "|", "^=": "^"}


class CompileError(Exception):
    pass


# ---------------------------------------------------------------------------
# 词法
# ---------------------------------------------------------------------------
PUNCT = ["<<=", ">>=",
         "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=", "%=",
         "<<", ">>", "&=", "|=", "^=",
         "+", "-", "*", "/", "%", "(", ")",
         "{", "}", ",", ";", "<", ">", "!", "=", "&", "|", "^", "[", "]"]
KEYWORDS = {"var", "on", "if", "else", "while", "break", "continue", "true", "false",
            "strs", "fn", "return"}


def tokenize(src):
    toks, i, line = [], 0, 1
    while i < len(src):
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if c in " \t\r":
            i += 1
            continue
        if c == "#":                                   # 行注释
            while i < len(src) and src[i] != "\n":
                i += 1
            continue
        if c.isdigit():
            j = i
            is_float = False
            if src[i:i + 2].lower() == "0x":
                j = i + 2
                while j < len(src) and src[j] in "0123456789abcdefABCDEF":
                    j += 1
                val = int(src[i:j], 16)
            else:
                while j < len(src) and src[j].isdigit():
                    j += 1
                # 浮点字面量: 小数点后面必须还有数字(否则 "1." 会吃掉后续的句点语义)
                if j + 1 < len(src) and src[j] == "." and src[j + 1].isdigit():
                    j += 1
                    while j < len(src) and src[j].isdigit():
                        j += 1
                    is_float = True
                val = float(src[i:j]) if is_float else int(src[i:j], 10)
            toks.append(("fnum" if is_float else "int", val, line))
            i = j
            continue
        if c.isalpha() or c == "_":
            j = i
            while j < len(src) and (src[j].isalnum() or src[j] == "_"):
                j += 1
            word = src[i:j]
            toks.append(("kw" if word in KEYWORDS else "id", word, line))
            i = j
            continue
        if c == '"':
            j = i + 1
            buf = []
            while j < len(src) and src[j] != '"':
                if src[j] == "\\" and j + 1 < len(src):
                    esc = src[j + 1]
                    buf.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(esc, esc))
                    j += 2
                    continue
                buf.append(src[j])
                j += 1
            if j >= len(src):
                raise CompileError(f"第 {line} 行: 字符串缺少结尾引号")
            toks.append(("str", "".join(buf), line))
            i = j + 1
            continue
        for p in PUNCT:
            if src.startswith(p, i):
                toks.append(("punct", p, line))
                i += len(p)
                break
        else:
            raise CompileError(f"第 {line} 行: 无法识别的字符 {c!r}")
    toks.append(("eof", "", line))
    return toks


# ---------------------------------------------------------------------------
# 语法(递归下降) -> AST
# AST 都是元组: ("num", v) ("var", name) ("bin", op, l, r) ("un", op, e)
#               ("call", name, [args]) ("store", name, expr)
#               ("if", cond, then[], else[]) ("while", cond, body[])
#               ("break",) ("continue",) ("exprstmt", expr)
# ---------------------------------------------------------------------------
class Parser:
    def __init__(self, toks):
        self.t = toks
        self.i = 0
        self.declared = set()      # 顶层名字: 重复定义检查 + 按钮常量遮蔽
        self.scopes = []           # 函数的参数/局部名字栈 —— **只在该函数内**遮蔽按钮常量

    def _shadowed(self, name):
        """这个名字当前可见吗(可见就不能当按钮常量解释)"""
        low = name.lower()
        if name in self.declared or low in self.declared:
            return True
        for sc in self.scopes:
            if name in sc or low in sc:
                return True
        return False

    def peek(self, k=0):
        return self.t[min(self.i + k, len(self.t) - 1)]

    def at(self, kind, val=None):
        k, v, _ = self.peek()
        return k == kind and (val is None or v == val)

    def next(self):
        tok = self.t[self.i]
        self.i += 1
        return tok

    def expect(self, kind, val=None):
        if not self.at(kind, val):
            _, v, ln = self.peek()
            raise CompileError(f"第 {ln} 行: 期望 {val or kind}, 实际是 {v!r}")
        return self.next()

    # --- 顶层 ---
    def parse_program(self):
        """返回 (decls, sections)。decls 是一串声明, 顺序就是内存分配顺序:
           (“scalar”, name, init) (“array”, name, count, init|None) (“strs”, name, [str])
           (“fn”, name, [(param, default|None)], body)"""
        decls, sections = [], {}
        while not self.at("eof"):
            if self.at("kw", "var") or self.at("kw", "strs"):
                decls.append(self.parse_var_or_strs(local=False))
                continue
            if self.at("kw", "fn"):
                decls.append(self.parse_fn())
                continue
            if self.at("kw", "on"):
                self.next()
                which = self.expect("id")[1]
                if which not in MAX_SECTIONS:
                    raise CompileError(f"未知的 on 段: {which}(只支持 start/update/render)")
                if which in sections:
                    raise CompileError(f"on {which} 重复定义")
                self.expect("punct", "{")
                sections[which] = self.parse_block()
                continue
            _, v, ln = self.peek()
            raise CompileError(f"第 {ln} 行: 顶层只允许 var / strs / fn / on 段, 实际是 {v!r}")
        return decls, sections

    # --- var / strs 声明(顶层与函数体内共用) ---
    def parse_var_or_strs(self, local):
        if self.at("kw", "var"):
            self.next()
            name = self.expect("id")[1]
            self._declare(name, local)
            if self.at("punct", "["):          # 数组: var m[64] [= 初值]
                self.next()
                count = self.expect("int")[1]
                self.expect("punct", "]")
                if not (1 <= count <= MAX_GLOBALS):
                    raise CompileError(f"数组 {name} 长度要在 1..{MAX_GLOBALS}")
                init = None
                if self.at("punct", "="):
                    self.next()
                    init = self.parse_expr()
                self.eat_semi()
                return ("larraydecl" if local else "array", name, count, init)
            self.expect("punct", "=")
            expr = self.parse_expr()
            self.eat_semi()
            return ("lvardecl" if local else "scalar", name, expr)
        # strs: 字符串数组, 给 img() 当"动画帧表"用, 下标可以是运行时的整数
        self.next()
        name = self.expect("id")[1]
        self._declare(name, local)
        self.expect("punct", "=")
        self.expect("punct", "{")
        items = []
        if not self.at("punct", "}"):
            items.append(self.expect("str")[1])
            while self.at("punct", ","):
                self.next()
                if self.at("punct", "}"):
                    break
                items.append(self.expect("str")[1])
        self.expect("punct", "}")
        self.eat_semi()
        if not items:
            raise CompileError(f"字符串数组 {name} 不能为空")
        return ("lstrsdecl" if local else "strs", name, items)

    def _declare(self, name, local):
        # 局部变量允许重名/遮蔽(每次声明都拿新槽); 顶层重名直接报错
        if local:
            if self.scopes:
                self.scopes[-1].add(name)
            return
        if name in self.declared:
            raise CompileError(f"名字 {name} 重复定义")
        self.declared.add(name)

    # --- 函数 ---
    def parse_fn(self):
        """fn name(p, q = 0.0) -> 0.0 { ... }
           参数后面跟 `= 字面量` 表示它是浮点(且是默认值); 省略 = 整数。
           `-> 字面量` 指定返回类型, 省略 = 整数。"""
        self.next()
        name = self.expect("id")[1]
        self._declare(name, False)
        self.expect("punct", "(")
        params = []
        self.scopes.append(set())          # 参数只在这个函数内部遮蔽按钮常量
        if not self.at("punct", ")"):
            while True:
                pn = self.expect("id")[1]
                self.scopes[-1].add(pn)
                default = None
                if self.at("punct", "="):
                    self.next()
                    default = self.parse_expr()
                params.append((pn, default))
                if self.at("punct", ","):
                    self.next()
                    continue
                break
        self.expect("punct", ")")
        rettype = None
        if self.at("punct", "-") and self.peek(1)[0] == "punct" and self.peek(1)[1] == ">":
            self.next()
            self.next()
            rettype = self.parse_expr()
            if rettype[0] not in ("num", "fnum"):
                raise CompileError(f"{name}: 返回类型要写成一个字面量, 例如 -> 0 或 -> 0.0")
        self.expect("punct", "{")
        body = self.parse_block()
        self.scopes.pop()
        return ("fn", name, params, rettype, body)

    def eat_semi(self):
        if self.at("punct", ";"):
            self.next()

    def parse_block(self):
        stmts = []
        while not self.at("punct", "}"):
            if self.at("eof"):
                raise CompileError("代码块缺少 '}'")
            stmts.append(self.parse_stmt())
        self.expect("punct", "}")
        return stmts

    def parse_stmt(self):
        if self.at("kw", "var") or self.at("kw", "strs"):
            return self.parse_var_or_strs(local=True)
        if self.at("kw", "return"):
            self.next()
            expr = None
            if not (self.at("punct", "}") or self.at("punct", ";") or self.at("eof")):
                expr = self.parse_expr()
            self.eat_semi()
            return ("return", expr)
        if self.at("kw", "if"):
            self.next()
            cond = self.parse_expr()
            self.expect("punct", "{")
            then = self.parse_block()
            other = []
            if self.at("kw", "else"):
                self.next()
                if self.at("kw", "if"):
                    other = [self.parse_stmt()]        # else if -> 当嵌套 if 处理
                else:
                    self.expect("punct", "{")
                    other = self.parse_block()
            return ("if", cond, then, other)
        if self.at("kw", "while"):
            self.next()
            cond = self.parse_expr()
            self.expect("punct", "{")
            return ("while", cond, self.parse_block())
        if self.at("kw", "break"):
            self.next()
            self.eat_semi()
            return ("break",)
        if self.at("kw", "continue"):
            self.next()
            self.eat_semi()
            return ("continue",)
        if self.at("punct", "{"):
            self.next()
            return ("block", self.parse_block())
        # 数组元素赋值: m[i] = v / m[i] += v ...
        if self.at("id") and self.peek(1)[0] == "punct" and self.peek(1)[1] == "[":
            ln = self.peek()[2]
            name = self.next()[1]
            self.next()
            index = self.parse_expr()
            self.expect("punct", "]")
            if not (self.at("punct") and self.peek()[1] in ASSIGN_OPS):
                raise CompileError(f"第 {ln} 行: 数组元素只能用于赋值, 例如 m[i] = v")
            op = self.next()[1]
            value = self.parse_expr()
            self.eat_semi()
            if op == "=":
                return ("idxstore", name, index, value)
            return ("idxop", name, index, COMPOUND[op], value)
        # 赋值(含复合赋值) 或 表达式语句
        if self.at("id") and self.peek(1)[0] == "punct" and \
                self.peek(1)[1] in ASSIGN_OPS:
            name = self.next()[1]
            op = self.next()[1]
            expr = self.parse_expr()
            self.eat_semi()
            if op == "=":
                return ("store", name, expr)
            return ("store", name, ("bin", COMPOUND[op], ("var", name), expr))
        expr = self.parse_expr()
        self.eat_semi()
        return ("exprstmt", expr)

    def parse_expr(self):
        return self.parse_or()

    def binary(self, sub, ops):
        node = sub()
        while self.at("punct") and self.peek()[1] in ops:
            op = self.next()[1]
            node = ("bin", op, node, sub())
        return node

    def parse_or(self):
        return self.binary(self.parse_and, ("||",))

    def parse_and(self):
        return self.binary(self.parse_cmp, ("&&",))

    # 比较放在位运算**之上**(也就是优先级更低):
    #   `x & 1 == 0` 解析成 `(x & 1) == 0` —— C 里是 `x & (1 == 0)`(恒 0), 那个坑太经典了。
    # 代价是 `a == b | c` 会理解成 `a == (b | c)`, 想先比较就加括号。
    def parse_cmp(self):
        return self.binary(self.parse_bor, ("==", "!=", "<", "<=", ">", ">="))

    def parse_bor(self):
        return self.binary(self.parse_bxor, ("|",))

    def parse_bxor(self):
        return self.binary(self.parse_band, ("^",))

    def parse_band(self):
        return self.binary(self.parse_shift, ("&",))

    def parse_shift(self):
        return self.binary(self.parse_add, ("<<", ">>"))

    def parse_add(self):
        return self.binary(self.parse_mul, ("+", "-"))

    def parse_mul(self):
        return self.binary(self.parse_unary, ("*", "/", "%"))

    def parse_unary(self):
        if self.at("punct", "-"):
            self.next()
            return ("un", "-", self.parse_unary())
        if self.at("punct", "!"):
            self.next()
            return ("un", "!", self.parse_unary())
        return self.parse_primary()

    def parse_primary(self):
        k, v, ln = self.peek()
        if k == "punct" and v == "(":          # 括号分组
            self.next()
            inner = self.parse_expr()
            self.expect("punct", ")")
            return inner
        if k == "str":
            self.next()
            return ("str", v)
        if k == "int":
            self.next()
            return ("num", v)
        if k == "fnum":
            self.next()
            return ("fnum", v)
        if k == "kw" and v in ("true", "false"):
            self.next()
            return ("num", 1 if v == "true" else 0)
        if k == "id":
            self.next()
            if self.at("punct", "("):
                self.next()
                args = []
                if not self.at("punct", ")"):
                    args.append(self.parse_expr())
                    while self.at("punct", ","):
                        self.next()
                        args.append(self.parse_expr())
                self.expect("punct", ")")
                return ("call", v, args)
            if self.at("punct", "["):          # 数组元素读
                self.next()
                index = self.parse_expr()
                self.expect("punct", "]")
                return ("idx", v, index)
            # 按钮常量: 只有当前作用域里没有同名名字时才当常量
            # (否则 var x 会被 X 按钮吃掉; 参数的遮蔽只限自己函数内部)
            if v.lower() in BUTTONS and not self._shadowed(v):
                return ("num", BUTTONS[v.lower()])
            return ("var", v)
        raise CompileError(f"第 {ln} 行: 期望表达式, 实际是 {v!r}")


# ---------------------------------------------------------------------------
# 代码生成
#
# 类型: 只有 int 与 float 两种。类型在编译期完全确定, 运行时同一个 32 位 cell
# 直接存 IEEE-754 位模式, 所以浮点没有任何装箱/标签开销。
# 内存: 标量与数组共用 VM 的 globals_ 池, 声明顺序就是分配顺序。
# ---------------------------------------------------------------------------
class CodeGen:
    def __init__(self):
        self.code = bytearray()
        self.labels = {}
        self.fixups = []          # (pos, label)
        self.strings = []
        self.str_index = {}
        self.loop_stack = []      # [break_label, continue_label]
        self.serial = 0
        # 符号表
        self.scalars = {}         # 顶层标量 name -> 下标
        self.arrays = {}          # 顶层数组 name -> (base, count)
        self.strs = {}            # 顶层字符串数组 name -> (base, count)
        self.vartype = {}         # 顶层 name -> T_I / T_F
        self.tmp = 0              # 隐藏临时槽(数组复合赋值 / 数组初值填充)
        self.nwords = 0
        # 函数
        self.fns = {}             # name -> {pnames, ptypes, defaults, rtype, pslots, ret, body, label}
        self.fn_order = []
        self.locals = []          # 当前函数的作用域栈(现在只用一层)
        self.cur_fn = None

    def bump(self, k=1):
        """分配 k 个内存字, 返回基址。全局/参数/局部共用同一块内存池。"""
        if self.nwords + k > MAX_GLOBALS:
            raise CompileError(f"变量总量超过 {MAX_GLOBALS} 个字(全局 + 数组 + 函数参数与局部)")
        base = self.nwords
        self.nwords += k
        return base

    def lookup(self, name):
        """找名字: 先局部(含参数), 再全局。返回 (kind, base, count, type) 或 None"""
        for sc in reversed(self.locals):
            if name in sc:
                return sc[name]
        if name in self.scalars:
            return ("scalar", self.scalars[name], None, self.vartype[name])
        if name in self.arrays:
            base, count = self.arrays[name]
            return ("array", base, count, self.vartype[name])
        if name in self.strs:
            base, count = self.strs[name]
            return ("strs", base, count, None)
        return None

    def define_local(self, name, kind, base, count, t):
        if not self.locals:
            raise CompileError("内部错误: 没有活动函数作用域")
        self.locals[-1][name] = (kind, base, count, t)

    def __len__(self):            # 让调用方沿用 len(...) 拿"内存字数量"
        return self.nwords

    # --- 符号分配(声明顺序 = 内存顺序) ---
    def alloc(self, decls):
        for d in decls:
            kind = d[0]
            if kind == "fn":
                fname, params, rettype = d[1], d[2], d[3]
                self.fns[fname] = {
                    "pnames": [p[0] for p in params],
                    "ptypes": [self.typeof(p[1]) if p[1] is not None else T_I for p in params],
                    "defaults": [p[1] for p in params],
                    "rtype": self.typeof(rettype) if rettype is not None else T_I,
                    "pslots": [], "ret": 0, "body": d[4], "label": f"fn_{fname}",
                    "ready": False,          # 函数体发射完了才能被调(0 参数的 pslots 也是空的)
                }
                self.fn_order.append(fname)
                continue
            if kind == "strs":
                self.strs[d[1]] = (self.strblock(d[2]), len(d[2]))
                continue
            if kind == "scalar":
                name, init = d[1], d[2]
                self.vartype[name] = self.typeof(init)
                self.scalars[name] = self.bump(1)
            else:                                     # array
                name, count, init = d[1], d[2], d[3]
                self.vartype[name] = self.typeof(init) if init is not None else T_I
                self.arrays[name] = (self.bump(count), count)
        self.tmp = self.bump(1)                       # 隐藏临时槽

    # --- 函数体 ---
    def emit_functions(self):
        """每个函数一块固定的内存槽(返回值 + 参数 + 局部), 所以 VM 只需要一个返回地址栈,
           **不支持递归**(参数槽会被自己盖掉)。函数必须定义在调用之前。"""
        for fname in self.fn_order:
            f = self.fns[fname]
            f["ret"] = self.bump(1)
            scope = {}
            for pn, ptype in zip(f["pnames"], f["ptypes"]):
                slot = self.bump(1)
                f["pslots"].append(slot)
                scope[pn] = ("scalar", slot, None, ptype)
            self.locals.append(scope)
            self.cur_fn = f
            saved_loops = self.loop_stack
            self.loop_stack = []

            self.mark(f["label"])
            for s in f["body"]:
                self.stmt(s)
            # 没写 return 会落到这里: 返回 0(0 的位模式同时是 0 和 0.0f)
            self.u8(OP_PUSH)
            self.i32(0)
            self.u8(OP_STMEM)
            self.u16(f["ret"])
            self.u8(OP_RET)

            self.loop_stack = saved_loops
            self.locals.pop()
            self.cur_fn = None
            f["ready"] = True

    def array_of(self, name):
        sym = self.lookup(name)
        if sym is None or sym[0] != "array":
            if sym is not None and sym[0] == "strs":
                raise CompileError(f"{name} 是字符串数组, 只能当 img() 的第 1 个参数")
            raise CompileError(f"未定义的数组: {name}(用 var {name}[n] 声明)")
        return sym[1], sym[2]

    def elem_type(self, name):
        sym = self.lookup(name)
        if sym is None or sym[0] != "array":
            raise CompileError(f"未定义的数组: {name}")
        return sym[3]

    def strs_block(self, name):
        sym = self.lookup(name)
        if sym is not None and sym[0] == "strs":
            return sym[1], sym[2]
        return None

    # --- 表达式类型 ---
    def typeof(self, node):
        k = node[0]
        if k == "num":
            return T_I
        if k == "fnum":
            return T_F
        if k == "var":
            name = node[1]
            sym = self.lookup(name)
            if sym is None:
                raise CompileError(f"未定义的变量: {name}(用 var 先声明)")
            if sym[0] == "array":
                raise CompileError(f"{name} 是数组, 要写成 {name}[i]")
            if sym[0] == "strs":
                raise CompileError(f"{name} 是字符串数组, 只能当 img() 的第 1 个参数")
            return sym[3]
        if k == "idx":
            sym = self.lookup(node[1])
            if sym is None or sym[0] != "array":
                raise CompileError(f"未定义的数组: {node[1]}(用 var {node[1]}[n] 声明)")
            return sym[3]
        if k == "un":
            return T_I if node[1] == "!" else self.typeof(node[2])
        if k == "bin":
            op = node[1]
            lt, rt = self.typeof(node[2]), self.typeof(node[3])
            if op in ("%", "&", "|", "^", "<<", ">>"):
                if lt == T_F or rt == T_F:
                    raise CompileError(f"运算符 {op} 只能用于整数")
                return T_I
            if op in ("<", "<=", ">", ">=", "==", "!=", "&&", "||"):
                return T_I
            return T_F if (lt == T_F or rt == T_F) else T_I
        if k == "call":
            if node[1] in self.fns:
                return self.fns[node[1]]["rtype"]
            key = node[1].lower()
            if key == "chr":
                return T_I
            if key in NUM_BUILTINS:
                return NUM_BUILTINS[key][2]
            if key in STRNUM_BUILTINS:
                return T_I
            raise CompileError(f"{node[1]}() 没有返回值, 只能当语句用")
        raise CompileError(f"内部错误: 未知表达式 {k}")

    # --- 基础输出 ---
    def new_label(self, prefix):
        self.serial += 1
        return f"{prefix}_{self.serial}"

    def here(self):
        return len(self.code)

    def mark(self, name):
        self.labels[name] = len(self.code)

    def u8(self, v):
        self.code += struct.pack("<B", v & 0xFF)

    def u16(self, v):
        self.code += struct.pack("<H", v & 0xFFFF)

    def i32(self, v):
        self.code += struct.pack("<i", v)

    @staticmethod
    def f32bits(v):
        """float 字面量 -> 它的 IEEE-754 位模式(当成 i32 塞进字节码)"""
        return struct.unpack("<i", struct.pack("<f", v))[0]

    def jump(self, op, label):
        self.u8(op)
        self.fixups.append((len(self.code), label))
        self.code += b"\x00\x00"

    def strref(self, s):
        if s not in self.str_index:
            self.str_index[s] = len(self.strings)
            self.strings.append(s)
        return self.str_index[s]

    def strblock(self, items):
        """给字符串数组开一段**连续**的字符串下标(允许重复, 保证 base+idx 对得上)"""
        base = len(self.strings)
        for s in items:
            self.strings.append(s)
            self.str_index.setdefault(s, len(self.strings) - 1)
        return base

    def resolve(self):
        for pos, label in self.fixups:
            if label not in self.labels:
                raise CompileError(f"内部错误: 标签 {label} 未定义")
            addr = self.labels[label]
            if addr > 0xFFFF:
                raise CompileError("字节码超过 64KB, 请拆分游戏逻辑")
            self.code[pos:pos + 2] = struct.pack("<H", addr)

    # --- 表达式: 结果压栈, 返回类型 ---
    def expr(self, node):
        kind = node[0]
        if kind == "str":
            raise CompileError("字符串只能作为内置函数的字面量参数(如 img/text/sfx/bgm)")
        if kind == "num":
            self.u8(OP_PUSH)
            self.i32(node[1])
            return T_I
        if kind == "fnum":
            self.u8(OP_PUSH)
            self.i32(self.f32bits(node[1]))
            return T_F
        if kind == "var":
            name = node[1]
            sym = self.lookup(name)
            if sym is None:
                raise CompileError(f"未定义的变量: {name}(用 var 先声明)")
            if sym[0] == "array":
                raise CompileError(f"{name} 是数组, 要写成 {name}[i]")
            if sym[0] == "strs":
                raise CompileError(f"{name} 是字符串数组, 只能当 img() 的第 1 个参数")
            self.load_slot(sym[1])
            return sym[3]
        if kind == "idx":
            base, _ = self.array_of(node[1])
            self.emit(node[2], T_I)                  # 下标一律按 int
            self.u8(OP_LDIDX)
            self.u16(base)
            return self.elem_type(node[1])
        if kind == "un":
            if node[1] == "!":
                t = self.expr(node[2])
                self.u8(OP_FLNOT if t == T_F else OP_LNOT)
                return T_I
            t = self.expr(node[2])
            self.u8(OP_FNEG if t == T_F else OP_NEG)
            return t
        if kind == "bin":
            return self.binop(node)
        if kind == "call":
            return self.call(node[1], node[2])
        raise CompileError(f"内部错误: 未知表达式 {kind}")

    def emit(self, node, want=None):
        """求值到栈上; want 非空时先转成该类型(int<->float)"""
        got = self.expr(node)
        if want is not None and want != got:
            self.u8(OP_ITOF if want == T_F else OP_FTOI)

    def load_slot(self, i):
        if i <= 0xFF:
            self.u8(OP_LOADG)
            self.u8(i)
        else:                                        # 槽位也可能超过 u8
            self.u8(OP_LDMEM)
            self.u16(i)

    def store_slot(self, i):
        if i <= 0xFF:
            self.u8(OP_STOREG)
            self.u8(i)
        else:
            self.u8(OP_STMEM)
            self.u16(i)

    def binop(self, node):
        op, ln, rn = node[1], node[2], node[3]
        lt, rt = self.typeof(ln), self.typeof(rn)

        if op in ("&", "|", "^", "<<", ">>"):
            self.emit(ln, T_I)
            self.emit(rn, T_I)
            self.u8({"&": OP_AND, "|": OP_OR, "^": OP_XOR,
                     "<<": OP_SHL, ">>": OP_SHR}[op])
            return T_I
        if op == "%":
            self.emit(ln, T_I)
            self.emit(rn, T_I)
            self.u8(OP_MOD)
            return T_I
        if op in ("&&", "||"):
            t = self.promote(lt, rt)
            self.emit(ln, t)
            self.emit(rn, t)
            if t == T_F:
                self.u8(OP_FLAND if op == "&&" else OP_FLOR)
            else:
                self.u8(OP_LAND if op == "&&" else OP_LOR)
            return T_I

        CMP_I = {"<": OP_LT, "<=": OP_LE, ">": OP_GT, ">=": OP_GE, "==": OP_EQ, "!=": OP_NE}
        if op in CMP_I:
            t = self.promote(lt, rt)
            self.emit(ln, t)
            self.emit(rn, t)
            if t == T_F:
                self.u8({OP_LT: OP_FLT, OP_LE: OP_FLE, OP_GT: OP_FGT,
                         OP_GE: OP_FGE, OP_EQ: OP_FEQ, OP_NE: OP_FNE}[CMP_I[op]])
            else:
                self.u8(CMP_I[op])
            return T_I

        t = self.promote(lt, rt)
        self.emit(ln, t)
        self.emit(rn, t)
        if t == T_F:
            self.u8({"+": OP_FADD, "-": OP_FSUB, "*": OP_FMUL, "/": OP_FDIV}[op])
        else:
            self.u8({"+": OP_ADD, "-": OP_SUB, "*": OP_MUL, "/": OP_DIV}[op])
        return t

    @staticmethod
    def promote(lt, rt):
        """只要有一边是浮点, 整个运算就按浮点做(另一边插 ITOF)"""
        return T_F if (lt == T_F or rt == T_F) else T_I

    def call(self, name, args):
        fn = self.fns.get(name)                      # 脚本自己定义的函数优先(区分大小写)
        if fn is not None:
            return self.user_call(name, fn, args)
        key = name.lower()
        if key == "chr":                            # chr(strs[i], pos) -> 字符码
            return self.chr_call(args)
        if key in NUM_BUILTINS:
            fid, argc, ret, atypes = NUM_BUILTINS[key]
            if len(args) != argc:
                raise CompileError(f"{name}() 需要 {argc} 个参数, 实际 {len(args)} 个")
            for a, at in zip(args, atypes):
                self.emit(a, at)
            self.u8(OP_CALLN)
            self.u8(fid)
            self.u8(argc)
            return ret
        if key in STRNUM_BUILTINS:                   # imgw/imgh: 名字是字面量, 返回 int
            if len(args) != 1 or args[0][0] != "str":
                raise CompileError(f'{key}() 用法: {key}("name")')
            self.u8(STRNUM_BUILTINS[key])
            self.u16(self.strref(args[0][1]))
            return T_I
        if key in STR_BUILTINS:
            self.str_call(key, args)
            return None
        raise CompileError(f"未知函数: {name}")

    def chr_call(self, args):
        """chr("str", pos) 或 chr(strs[i], pos) —— 用来把字符画直接写成字符串表。
           返回字符码(0 = 越界), 拿它和 ASCII 常量比。"""
        if len(args) != 2:
            raise CompileError('chr() 用法: chr(strs[i], pos) 或 chr("str", pos)')
        src = args[0]
        if src[0] == "str":
            self.u8(OP_PUSH)                         # idx 恒为 0
            self.i32(0)
            self.emit(args[1], T_I)
            self.u8(OP_SCHR)
            self.u16(self.strref(src[1]))
            self.u16(1)
            return T_I
        if src[0] != "idx":
            raise CompileError("chr() 的第 1 个参数必须是字符串或字符串数组元素")
        blk = self.strs_block(src[1])
        if blk is None:
            raise CompileError(f"{src[1]} 不是字符串数组(要用 strs 声明)")
        self.emit(src[2], T_I)                       # 哪个字符串
        self.emit(args[1], T_I)                      # 第几个字符
        self.u8(OP_SCHR)
        self.u16(blk[0])
        self.u16(blk[1])
        return T_I

    def user_call(self, name, fn, args):
        """调用脚本函数: 参数先全部求值到栈上, 再逆序弹进被调函数的参数槽。
           所有中间结果都留在值栈上, 所以 f(g(x), h(y)) 这种嵌套不会互相盖。"""
        if not fn["ready"]:
            raise CompileError(f"{name}() 必须定义在调用之前")
        np = len(fn["pnames"])
        if len(args) > np:
            raise CompileError(f"{name}() 最多 {np} 个参数, 实际 {len(args)} 个")
        for i, a in enumerate(args):
            self.emit(a, fn["ptypes"][i])
        for i in range(len(args), np):               # 缺的参数用声明时的默认值
            d = fn["defaults"][i]
            if d is None:
                raise CompileError(f"{name}() 第 {i + 1} 个参数没有默认值, 必须给")
            self.expr(d)
        for i in range(np - 1, -1, -1):
            self.u8(OP_STMEM)
            self.u16(fn["pslots"][i])
        self.jump(OP_CALL_USER, fn["label"])
        self.load_slot(fn["ret"])
        return fn["rtype"]

    def str_call(self, key, args):
        """img/text/sfx/bgm —— 无返回值, 只能当语句"""
        if key == "text":
            if len(args) != 5:
                raise CompileError('text() 需要 5 个参数: x, y, "字符串", color, scale')
            if args[2][0] != "str":
                raise CompileError("text() 的第 3 个参数必须是字符串字面量")
            self.emit(args[3], T_I)                  # color
            self.emit(args[1], T_I)                  # y
            self.emit(args[0], T_I)                  # x
            scale = args[4]
            if scale[0] != "num" or not (1 <= scale[1] <= 8):
                raise CompileError("text() 的 scale 必须是 1..8 的常量")
            self.u8(OP_TEXT)
            self.u16(self.strref(args[2][1]))
            self.u8(scale[1])
            return
        if key == "img":
            self.img_call(args)
            return
        # sfx / bgm
        if len(args) != 1 or args[0][0] != "str":
            raise CompileError(f'{key}() 用法: {key}("name")')
        self.u8(OP_SFX if key == "sfx" else OP_BGM)
        self.u16(self.strref(args[0][1]))

    def img_call(self, args):
        """img(name, x, y) / img(name, x, y, flip) / img(strs[i], x, y[, flip])
           flip 是运行时的(角色朝左/朝右共用一张图)"""
        if len(args) not in (3, 4):
            raise CompileError('img() 用法: img("name" 或 strs[i], x, y [, flip])')
        flip = args[3] if len(args) == 4 else ("num", 0)
        src = args[0]

        if src[0] == "str":                          # 名字写死
            self.emit(flip, T_I)
            self.emit(args[2], T_I)                  # y
            self.emit(args[1], T_I)                  # x
            if len(args) == 3:
                self.u8(OP_IMG)
            else:
                self.u8(OP_IMGF)
            self.u16(self.strref(src[1]))
            return

        if src[0] == "idx":                         # 名字来自字符串数组
            blk = self.strs_block(src[1])
            if blk is None:
                raise CompileError(f"{src[1]} 不是字符串数组(要用 strs 声明)")
            base, count = blk
            self.emit(flip, T_I)
            self.emit(src[2], T_I)                   # 帧下标
            self.emit(args[2], T_I)                  # y
            self.emit(args[1], T_I)                  # x
            self.u8(OP_IMGV)
            self.u16(base)
            self.u16(count)
            return

        raise CompileError("img() 的第 1 个参数必须是字符串字面量或字符串数组元素")

    # --- 语句 ---
    def stmt(self, node):
        kind = node[0]
        if kind == "lvardecl":                       # 函数内部的 var
            name, init = node[1], node[2]
            t = self.typeof(init)
            slot = self.bump(1)
            self.define_local(name, "scalar", slot, None, t)
            self.emit(init, t)
            self.store_slot(slot)
            return
        if kind == "larraydecl":                     # 函数内部的 var m[n]
            name, count, init = node[1], node[2], node[3]
            t = self.typeof(init) if init is not None else T_I
            base = self.bump(count)
            self.define_local(name, "array", base, count, t)
            if init is not None and not _is_zero(init):
                self.stmt(("__fill__", name, init))
            return
        if kind == "lstrsdecl":                      # 函数内部的 strs
            self.define_local(node[1], "strs", self.strblock(node[2]), len(node[2]), None)
            return
        if kind == "return":
            if self.cur_fn is None:
                # on 段里的裸 return = 这一段到这里就结束(比 for 一个 if 包住整段干净)
                if node[1] is not None:
                    raise CompileError("on 段里的 return 不能带返回值(它不是函数)")
                self.u8(OP_HALT)
                return
            if node[1] is not None:
                self.emit(node[1], self.cur_fn["rtype"])
            else:
                self.u8(OP_PUSH)
                self.i32(0)
            self.u8(OP_STMEM)
            self.u16(self.cur_fn["ret"])
            self.u8(OP_RET)
            return
        if kind == "store":
            name = node[1]
            sym = self.lookup(name)
            if sym is None:
                raise CompileError(f"未定义的变量: {name}(用 var 先声明)")
            if sym[0] == "array":
                raise CompileError(f"{name} 是数组, 要写成 {name}[i] = ...")
            if sym[0] == "strs":
                raise CompileError(f"{name} 是字符串数组, 不能赋值")
            self.emit(node[2], sym[3])
            self.store_slot(sym[1])
            return
        if kind == "idxstore":
            name, index, value = node[1], node[2], node[3]
            base, _ = self.array_of(name)
            self.emit(value, self.elem_type(name))   # 先值
            self.emit(index, T_I)                    # 后下标 -> 栈 [v, i]
            self.u8(OP_STIDX)
            self.u16(base)
            return
        if kind == "idxop":                          # m[i] += v 等
            name, index, op, value = node[1], node[2], node[3], node[4]
            base, _ = self.array_of(name)
            t = self.elem_type(name)
            rt = self.typeof(value)
            if op in ("%", "&", "|", "^", "<<", ">>"):
                res = T_I
            else:
                res = self.promote(t, rt)
            # 下标先存进隐藏临时槽 —— 只求值一次(避免 rnd() 这类被算两遍)
            self.emit(index, T_I)
            self.u8(OP_STMEM)
            self.u16(self.tmp)
            self.u8(OP_LDMEM)
            self.u16(self.tmp)
            self.u8(OP_LDIDX)
            self.u16(base)
            if t != res:                             # 读出来的值提升到运算类型
                self.u8(OP_ITOF if res == T_F else OP_FTOI)
            self.emit(value, res)
            if res == T_F:
                self.u8({"+": OP_FADD, "-": OP_FSUB, "*": OP_FMUL, "/": OP_FDIV}[op])
            else:
                self.u8({"+": OP_ADD, "-": OP_SUB, "*": OP_MUL, "/": OP_DIV,
                         "%": OP_MOD, "&": OP_AND, "|": OP_OR, "^": OP_XOR,
                         "<<": OP_SHL, ">>": OP_SHR}[op])
            if res != t:                             # 结果落回数组元素类型
                self.u8(OP_ITOF if t == T_F else OP_FTOI)
            self.u8(OP_LDMEM)
            self.u16(self.tmp)
            self.u8(OP_STIDX)
            self.u16(base)
            return
        if kind == "__fill__":                       # 数组初值: 用临时槽当计数器
            name, init = node[1], node[2]
            base, count = self.array_of(name)
            t = self.elem_type(name)
            self.u8(OP_PUSH)
            self.i32(0)
            self.u8(OP_STMEM)
            self.u16(self.tmp)
            top = self.new_label("fill_top")
            end = self.new_label("fill_end")
            self.mark(top)
            self.u8(OP_LDMEM)
            self.u16(self.tmp)
            self.u8(OP_PUSH)
            self.i32(count)
            self.u8(OP_LT)
            self.jump(OP_JZ, end)
            self.emit(init, t)
            self.u8(OP_LDMEM)
            self.u16(self.tmp)
            self.u8(OP_STIDX)
            self.u16(base)
            self.u8(OP_LDMEM)
            self.u16(self.tmp)
            self.u8(OP_PUSH)
            self.i32(1)
            self.u8(OP_ADD)
            self.u8(OP_STMEM)
            self.u16(self.tmp)
            self.jump(OP_JMP, top)
            self.mark(end)
            return
        if kind == "exprstmt":
            child = node[1]
            # img/text/sfx/bgm 不产生值(VM 里不压栈), 所以不能补 POP,
            # 否则会多弹走一个栈上的值。
            if child[0] == "call" and child[1].lower() in STR_BUILTINS:
                self.str_call(child[1].lower(), child[2])
            else:
                self.expr(child)
                self.u8(OP_POP)
            return
        if kind == "block":
            for s in node[1]:
                self.stmt(s)
            return
        if kind == "if":
            else_l = self.new_label("else")
            end_l = self.new_label("endif")
            self.emit(node[1], T_I)
            self.jump(OP_JZ, else_l)
            for s in node[2]:
                self.stmt(s)
            self.jump(OP_JMP, end_l)
            self.mark(else_l)
            for s in node[3]:
                self.stmt(s)
            self.mark(end_l)
            return
        if kind == "while":
            top = self.new_label("while_top")
            end = self.new_label("while_end")
            self.mark(top)
            self.emit(node[1], T_I)
            self.jump(OP_JZ, end)
            self.loop_stack.append([end, top])
            for s in node[2]:
                self.stmt(s)
            self.loop_stack.pop()
            self.jump(OP_JMP, top)
            self.mark(end)
            return
        if kind == "break":
            if not self.loop_stack:
                raise CompileError("break 不在 while 里")
            self.jump(OP_JMP, self.loop_stack[-1][0])
            return
        if kind == "continue":
            if not self.loop_stack:
                raise CompileError("continue 不在 while 里")
            self.jump(OP_JMP, self.loop_stack[-1][1])
            return
        raise CompileError(f"内部错误: 未知语句 {kind}")

    def section(self, stmts):
        """生成一段代码, 返回入口地址; 末尾补 HALT。\n\n           每个 on 段也有自己的作用域 —— 段里写 var 就是这一段的局部变量\n           (和函数里一样: 每次声明拿一个新槽, 不污染全局)。"""
        entry = self.here()
        self.locals.append({})
        try:
            for s in stmts or []:
                self.stmt(s)
        finally:
            self.locals.pop()
        self.u8(OP_HALT)
        return entry


# ---------------------------------------------------------------------------
def _is_zero(node):
    return node[0] == "num" and node[1] == 0 or node[0] == "fnum" and node[1] == 0


def compile_source(src):
    """返回 (code, entry{start/update/render: pc 或 None}, strings, gen)
       gen 支持 len() -> 需要的内存字数量(写进 .gbn 头的 globalCount)"""
    decls, sections = Parser(tokenize(src)).parse_program()
    gen = CodeGen()
    gen.alloc(decls)

    # 函数体先铺在前面 —— 每段都以 RET 收尾, 不会被顺序执行到
    gen.emit_functions()

    # 变量初值在 start 之前执行: 放进一个隐式 init 段
    init_stmts = []
    for d in decls:
        if d[0] == "scalar":
            init_stmts.append(("store", d[1], d[2]))
        elif d[0] == "array" and d[3] is not None and not _is_zero(d[3]):
            # 全 0 初值不用生成代码(VM 的 globals_ 本来就是清零的)
            init_stmts.append(("__fill__", d[1], d[3]))

    entries = {}
    if init_stmts:
        entries["init"] = gen.section(init_stmts)
    for name in ("start", "update", "render"):
        if name in sections:
            entries[name] = gen.section(sections[name])
    gen.resolve()
    return bytes(gen.code), entries, gen.strings, gen


def _fmt_push(v):
    """PUSH 的操作数可能是 int 也可能是 float 的位模式; 能当有限浮点读出来就标一下"""
    f = struct.unpack("<f", struct.pack("<i", v))[0]
    if f == f and abs(f) != float("inf") and f != 0.0 and abs(f) >= 1e-6 and abs(f) < 1e12:
        if f != int(f):
            return f"{v} (= {f:g}f)"
    return f"{v}"


def listing(code, entries, strings, labels=None):
    names = {}
    if labels:
        for lab, addr in labels.items():
            if lab.startswith("fn_"):
                names[addr] = "fn " + lab[3:]
    for k, v in entries.items():
        names[v] = k.upper()
    i, out = 0, []
    while i < len(code):
        start = i
        op = code[i]
        i += 1
        text = OP_NAMES.get(op, f"?0x{op:02X}")
        if op == OP_PUSH:
            (v,) = struct.unpack_from("<i", code, i)
            text += f" {_fmt_push(v)}"
            i += 4
        elif op in (OP_LOADG, OP_STOREG):
            text += f" g{code[i]}"
            i += 1
        elif op in (OP_LDMEM, OP_STMEM, OP_LDIDX, OP_STIDX, OP_IMGF, OP_IMGW, OP_IMGH):
            (a,) = struct.unpack_from("<H", code, i)
            text += f" {a}"
            i += 2
        elif op == OP_IMGV:
            (a, c) = struct.unpack_from("<HH", code, i)
            text += f" base={a} count={c}"
            i += 4
        elif op == OP_SCHR:
            (a, c) = struct.unpack_from("<HH", code, i)
            text += f" base={a} count={c}"
            i += 4
        elif op in (OP_JMP, OP_JZ, OP_JNZ, OP_CALL_USER):
            (a,) = struct.unpack_from("<H", code, i)
            text += f" -> 0x{a:04X}"
            if a in names:
                text += f" ({names[a]})"
            i += 2
        elif op == OP_CALLN:
            text += f" fn{code[i]} argc{code[i + 1]}"
            i += 2
        elif op in (OP_IMG, OP_SFX, OP_BGM):
            (s,) = struct.unpack_from("<H", code, i)
            text += f' "{strings[s]}"'
            i += 2
        elif op == OP_TEXT:
            (s,) = struct.unpack_from("<H", code, i)
            text += f' "{strings[s]}" scale={code[i + 2]}'
            i += 3
        mark = f"   ; <{names[start]}>" if start in names else ""
        out.append(f"{start:04X}  {text}{mark}")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(prog="gs_compiler", description="game.gs -> 字节码")
    ap.add_argument("src", help="脚本文件")
    ap.add_argument("-o", "--out", help="输出字节码(调试用, 正常应走 build_game.py)")
    ap.add_argument("--listing", action="store_true", help="打印反汇编")
    a = ap.parse_args()

    path = Path(a.src)
    if not path.is_file():
        print(f"找不到脚本: {path}")
        return 1
    try:
        code, entries, strings, gen = compile_source(path.read_text(encoding="utf-8"))
    except CompileError as exc:
        print(f"编译失败: {exc}")
        return 1

    scal = len(gen.scalars)
    elems = sum(c for _, c in gen.arrays.values())
    print(f"OK  {path.name}: {len(code)} 字节字节码, "
          f"{len(gen)} 个内存字(标量 {scal} + 数组元素 {elems} + 函数 {len(gen.fns)} 个 + 临时槽), "
          f"{len(strings)} 个字符串")
    print("  段: " + ", ".join(f"{k}@{v:04X}" for k, v in entries.items()))
    if gen.fn_order:
        print("  函数: " + ", ".join(f"{n}@{gen.labels['fn_' + n]:04X}" for n in gen.fn_order))
    if gen.arrays:
        print("  数组: " + ", ".join(f"{n}[{c}]@{b}" for n, (b, c) in gen.arrays.items()))
    if a.listing:
        print()
        print(listing(code, entries, strings, gen.labels))
    if a.out:
        Path(a.out).write_bytes(code)
        print(f"-> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
