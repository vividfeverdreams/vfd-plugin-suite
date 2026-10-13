#!/usr/bin/env python3
"""Fails if a plug-in source has a string literal with non-ASCII text that isn't decoded as UTF-8.

juce::String reads a plain "..." literal byte by byte, so an em dash or a middle dot shows up on screen as
junk such as "â€". Wrap such text in juce::String::fromUTF8("...") (or use plain ASCII).
Run from the suite root: python3 shared/tools/check_text_literals.py
"""
import os, re, sys

ROOTS = ["yoi-boi/Source", "throw-up/Source", "back-scratch/Source", "shifty/Source",
         "energy-harvester/Source", "stutter-step/Source", "shared"]
LITERAL = re.compile(r'(u8|L|u|U)?"((?:[^"\\\n]|\\.)*)"')

problems = []
for root in ROOTS:
    for folder, _, files in os.walk(root):
        for name in files:
            if not name.endswith((".cpp", ".h", ".mm")):
                continue
            path = os.path.join(folder, name)
            with open(path, encoding="utf-8", errors="replace") as source:
                for number, line in enumerate(source, 1):
                    if line.lstrip().startswith(("//", "*")):
                        continue
                    for match in LITERAL.finditer(line):
                        if not any(ord(c) > 127 for c in match.group(2)):
                            continue
                        before = line[:match.start()]
                        if match.group(1) or before.rstrip().endswith(("fromUTF8(", "CharPointer_UTF8(", "fromUTF8 (", "CharPointer_UTF8 (")):
                            continue
                        problems.append(f"{path}:{number}: {match.group(0)}")
print("\n".join(problems) if problems else "All non-ASCII string literals are decoded as UTF-8.")
sys.exit(1 if problems else 0)
