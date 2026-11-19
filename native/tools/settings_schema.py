#!/usr/bin/env python3
"""Generate native setting definitions from Crest's shared Lua settings schema."""

import argparse
import ast
import json
from pathlib import Path
import re


def rows(source, field):
    start = re.search(rf"^M\.{field}\s*=\s*(?=\{{)", source, re.M).end()
    tokens = re.compile(r'''"[^"]*"|'[^']*'|\{|\}|true\b|false\b''')
    substitutions = {"{": "[", "}": "]", "true": "True", "false": "False"}
    depth = 0
    for match in tokens.finditer(source, start):
        depth += (match[0] == "{") - (match[0] == "}")
        if depth == 0:
            data = source[start:match.end()]
            return ast.literal_eval(tokens.sub(lambda item: substitutions.get(item[0], item[0]), data))
    raise ValueError(f"Unterminated Crest {field} table")


def generate(source):
    fields = [row[1:] for row in rows(source, "schema")]
    for unit in rows(source, "units"):
        fields.extend([f"{unit[0]}_{key}", *rest] for key, *rest in rows(source, "unit_fields"))
    yielded = ["// Generated from crest/config.lua by native/tools/settings_schema.py."]
    for key, _, kind, default, *limits in fields:
        low, high, choices = 0, 0, ""
        if kind == "number":
            low, high = limits
        elif kind == "color":
            low, high = 0, 0xffffff
        elif kind == "string":
            high = limits[0]
        elif kind == "enum":
            choices = "|".join(limits[0])
        elif kind != "bool":
            raise ValueError(f"Unknown setting type {kind}")
        initial = json.dumps(default if isinstance(default, str) else json.dumps(default))
        yielded.append(f'    {{"{key}", Kind::{kind.upper()}, {initial}, {low}, {high}, {json.dumps(choices)}}},')
    if len({row[0] for row in fields}) != len(fields):
        raise ValueError("Duplicate Crest schema key")
    return "\n".join(yielded) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    content = generate(args.source.read_text(encoding="utf-8"))
    args.output.write_bytes(content.encode("utf-8"))


if __name__ == "__main__":
    main()
