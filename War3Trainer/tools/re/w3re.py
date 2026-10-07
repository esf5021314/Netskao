# -*- coding: utf-8 -*-
"""
Game.dll 分析小工具（本工程核对地址时用的就是这些命令）

依赖：  pip install capstone pefile

用法：
    python w3re.py dis   <Game.dll> <RVA> [条数]        从 RVA 开始反汇编
    python w3re.py func  <Game.dll> <RVA>               反汇编 RVA 所在的整个函数（标出 RVA）
    python w3re.py xref  <Game.dll> <RVA>               查找 call/jmp rel32 到该 RVA 的位置
    python w3re.py imm   <Game.dll> <RVA>               查找以绝对地址（基址+RVA）作为立即数的位置
    python w3re.py sig   <Game.dll> "6A 01 ?? 8B"       在代码段搜索特征码（?? 为通配）
    python w3re.py bytes <Game.dll> <RVA> <长度>         打印原始字节（补丁表的“原始字节”就是这样取的）
    python w3re.py rtti  <Game.dll> <类名>               由 RTTI 名称（如 CWorldFrameWar3）找虚表地址
    python w3re.py order <Game.dll> <命令字符串>          由命令字符串（如 innerfire）找命令 ID

所有 RVA 均为十六进制，相对 Game.dll 基址。
"""
import re
import struct
import sys

import capstone
import pefile


class Module:
    def __init__(self, path):
        self.pe = pefile.PE(path, fast_load=True)
        self.img = self.pe.get_memory_mapped_image()
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        text = [s for s in self.pe.sections if s.Name.startswith(b'.text')][0]
        self.text_lo = text.VirtualAddress
        self.text_hi = text.VirtualAddress + text.Misc_VirtualSize
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def disasm(self, rva, count=20, mark=None):
        n = 0
        for ins in self.md.disasm(self.img[rva:rva + count * 16], self.base + rva):
            r = ins.address - self.base
            flag = '>>' if mark is not None and r == mark else '  '
            print('%s%#08x: %-20s %s %s' % (flag, r, ins.bytes.hex(), ins.mnemonic, ins.op_str))
            n += 1
            if n >= count:
                break

    def func_start(self, rva):
        # 向前找填充字节（int3 / nop）作为函数边界
        s = rva
        while s > self.text_lo and not (self.img[s - 1] in (0xCC, 0x90) and self.img[s - 2] in (0xCC, 0x90, 0xC3)):
            s -= 1
            if rva - s > 0x4000:
                break
        return s

    def xref(self, target):
        res = []
        for i in range(self.text_lo, self.text_hi - 5):
            if self.img[i] in (0xE8, 0xE9):
                rel = struct.unpack_from('<i', self.img, i + 1)[0]
                if i + 5 + rel == target:
                    res.append((i, 'call' if self.img[i] == 0xE8 else 'jmp'))
        return res

    def imm(self, target):
        pat = struct.pack('<I', self.base + target)
        return [m.start() for m in re.finditer(re.escape(pat), self.img[self.text_lo:self.text_hi])]

    def sig(self, text):
        pat = b''.join(b'.' if t == '??' else re.escape(bytes([int(t, 16)])) for t in text.split())
        return [self.text_lo + m.start() for m in re.finditer(pat, self.img[self.text_lo:self.text_hi], re.S)]

    def rtti(self, name):
        i = self.img.find(('.?AV%s@@' % name).encode())
        if i < 0:
            return None
        td = self.base + i - 8
        for m in re.finditer(re.escape(struct.pack('<I', td)), self.img):
            col = m.start() - 12
            if struct.unpack_from('<I', self.img, col)[0] != 0:
                continue
            for v in re.finditer(re.escape(struct.pack('<I', self.base + col)), self.img):
                return v.start() + 4
        return None

    def order(self, name):
        i = self.img.find(b'\0' + name.encode() + b'\0')
        if i < 0:
            return None
        va = struct.pack('<I', self.base + i + 1)
        for m in re.finditer(re.escape(b'\x68' + va + b'\x68'), self.img):
            return struct.unpack_from('<I', self.img, m.start() + 6)[0]
        return None


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 1
    cmd, mod = argv[1], Module(argv[2])
    if cmd == 'dis':
        mod.disasm(int(argv[3], 16), int(argv[4]) if len(argv) > 4 else 20)
    elif cmd == 'func':
        rva = int(argv[3], 16)
        start = mod.func_start(rva)
        print('函数起点 %#x' % start)
        mod.disasm(start, 400, mark=rva)
    elif cmd == 'xref':
        for a, k in mod.xref(int(argv[3], 16)):
            print('%#08x %s' % (a, k))
    elif cmd == 'imm':
        for a in mod.imm(int(argv[3], 16)):
            print('%#08x' % (mod.text_lo + a))
    elif cmd == 'sig':
        hits = mod.sig(argv[3])
        for a in hits[:50]:
            print('%#08x' % a)
        print('共 %d 处' % len(hits))
    elif cmd == 'bytes':
        rva, n = int(argv[3], 16), int(argv[4])
        print(' '.join('%02X' % b for b in mod.img[rva:rva + n]))
    elif cmd == 'rtti':
        vt = mod.rtti(argv[3])
        print('虚表 RVA = %s' % (hex(vt) if vt is not None else '未找到'))
    elif cmd == 'order':
        oid = mod.order(argv[3])
        print('命令 ID = %s' % ('%d (0x%X)' % (oid, oid) if oid else '未找到'))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
