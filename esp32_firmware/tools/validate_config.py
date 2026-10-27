#!/usr/bin/env python3
"""Validate a config document against config.schema.json.

Tiny validator for the JSON Schema subset the schema uses (type, const, enum, pattern, min/max,
minLength/maxLength, minItems/maxItems, items, properties, required, additionalProperties, not, $ref/$defs),
so it runs with the Python standard library only. For full Draft 2020-12 semantics use `jsonschema`.

Usage: python3 tools/validate_config.py [config.json]      (default: config/config.example.json)
Exit code 0 = valid.
"""
import json, re, sys, pathlib

HERE = pathlib.Path(__file__).resolve().parent.parent / "config"

def resolve(schema, root):
    while "$ref" in schema:
        node = root
        for part in schema["$ref"].lstrip("#/").split("/"):
            node = node[part]
        schema = node
    return schema

def is_type(v, t):
    if t == "object": return isinstance(v, dict)
    if t == "array": return isinstance(v, list)
    if t == "string": return isinstance(v, str)
    if t == "boolean": return isinstance(v, bool)
    if t == "integer": return isinstance(v, int) and not isinstance(v, bool)
    if t == "number": return isinstance(v, (int, float)) and not isinstance(v, bool)
    return False

def validate(v, schema, root, path="$"):
    errs = []
    schema = resolve(schema, root)
    if "const" in schema and v != schema["const"]: errs.append(f"{path}: must be {schema['const']!r}")
    if "enum" in schema and v not in schema["enum"]: errs.append(f"{path}: {v!r} not in {schema['enum']}")
    if "not" in schema and not validate(v, schema["not"], root, path): errs.append(f"{path}: {v!r} matches a forbidden form")
    if "type" in schema and not is_type(v, schema["type"]):
        return errs + [f"{path}: expected {schema['type']}, got {type(v).__name__}"]
    if isinstance(v, str):
        if "pattern" in schema and not re.search(schema["pattern"], v): errs.append(f"{path}: {v!r} does not match {schema['pattern']}")
        if len(v.encode()) > schema.get("maxLength", 10**9) and "maxLength" in schema and len(v) > schema["maxLength"]:
            errs.append(f"{path}: longer than {schema['maxLength']}")
        if len(v) < schema.get("minLength", 0): errs.append(f"{path}: shorter than {schema['minLength']}")
    if isinstance(v, (int, float)) and not isinstance(v, bool):
        if "minimum" in schema and v < schema["minimum"]: errs.append(f"{path}: {v} < {schema['minimum']}")
        if "maximum" in schema and v > schema["maximum"]: errs.append(f"{path}: {v} > {schema['maximum']}")
    if isinstance(v, list):
        if len(v) < schema.get("minItems", 0): errs.append(f"{path}: fewer than {schema['minItems']} items")
        if "maxItems" in schema and len(v) > schema["maxItems"]: errs.append(f"{path}: more than {schema['maxItems']} items")
        if "items" in schema:
            for i, x in enumerate(v): errs += validate(x, schema["items"], root, f"{path}[{i}]")
    if isinstance(v, dict):
        props = schema.get("properties", {})
        for k in schema.get("required", []):
            if k not in v: errs.append(f"{path}: missing required '{k}'")
        for k, x in v.items():
            if k in props: errs += validate(x, props[k], root, f"{path}.{k}")
            elif schema.get("additionalProperties") is False: errs.append(f"{path}: unknown property '{k}'")
    return errs

def main(argv):
    schema = json.loads((HERE / "config.schema.json").read_text())
    doc_path = pathlib.Path(argv[1]) if len(argv) > 1 else HERE / "config.example.json"
    errs = validate(json.loads(doc_path.read_text()), schema, schema)
    if errs:
        print(f"{doc_path}: INVALID"); [print("  -", e) for e in errs]; return 1
    print(f"{doc_path}: valid against config.schema.json"); return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
