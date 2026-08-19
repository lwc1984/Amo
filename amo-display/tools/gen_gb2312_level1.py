"""生成 gb2312_level1.txt —— GB2312 一级汉字 + 全角标点。

区 16-55 是一级汉字（按拼音排列，3755 字），区 01/03 是全角标点与全角字母数字。
文件名沿用 level1 是历史原因，实际内容比一级汉字多一段标点。

GB2312-1980 是一个 94x94 的区位编码表：区（行）01-09 是符号，16-55 是一级汉字
（常用字，按拼音排序），56-87 是二级汉字（次常用字，按部首/笔画排序）。
本表只取一级，覆盖日常人名/地名/术语的绝大多数。

区位号到 GB2312 双字节编码的映射：byte1 = 0xA0 + 区，byte2 = 0xA0 + 位（位从 1 起）。
Python 标准库的 "gb2312" 编解码器（实际是 CPython 里的 gb2312 编码表）可以把这两个
字节直接解码成对应的汉字；不在表里的区位组合会抛 UnicodeDecodeError，跳过即可。

重新生成：
    python amo-display/tools/gen_gb2312_level1.py

输出覆盖 amo-display/tools/gb2312_level1.txt（一行一个字，UTF-8 无 BOM，LF 换行）。
`gen_fonts.ps1` 读取这个文件作为 lv_font_cjk_16 的 --symbols 输入。
"""
import io
import os

OUT_PATH = os.path.join(os.path.dirname(__file__), "gb2312_level1.txt")


def rows_to_chars(rows):
    chars = []
    for row in rows:
        for col in range(1, 95):  # 位 1-94
            b1 = 0xA0 + row
            b2 = 0xA0 + col
            try:
                c = bytes([b1, b2]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            chars.append(c)
    return chars


def gb2312_level1_chars():
    return rows_to_chars(range(16, 56))          # 一级汉字：区 16-55


def punctuation_chars():
    """区 01 与区 03：全角标点与全角字母数字。

    最初只取了一级汉字，结果实测在屏幕上看到「要不要把这个目录删掉□」——
    句末的「？」是缺字方框。中文句子里到处是 ，。？！：「」——，缺了它们
    每一句都会长出方框，而这些字符只有区 01 才有。

    区 02（序号）、04/05（日文假名）、06/07（希腊西里尔）、08（拼音注音）、
    09（制表符）不收 —— 这个项目用不到，而每个字形都要占 flash。
    """
    return rows_to_chars([1, 3])


def main():
    chars = gb2312_level1_chars()
    assert len(chars) == 3755, f"expected 3755 level-1 chars, got {len(chars)}"
    puncts = punctuation_chars()
    chars = puncts + chars
    with io.open(OUT_PATH, "w", encoding="utf-8", newline="\n") as f:
        for c in chars:
            f.write(c + "\n")
    print(f"wrote {len(chars)} chars to {OUT_PATH}"
          f"  ({len(puncts)} 标点/全角 + 3755 一级汉字)")


if __name__ == "__main__":
    main()
