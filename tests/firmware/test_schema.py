#!/usr/bin/env python3
"""The example config must validate, and a set of deliberately bad documents must be rejected.
(Uses the stdlib validator in esp32_firmware/tools; the firmware itself repeats these checks and more.)"""
import copy, importlib.util, json, pathlib, sys

root = pathlib.Path(__file__).resolve().parent.parent.parent
spec = importlib.util.spec_from_file_location("vc", root / "esp32_firmware/tools/validate_config.py")
vc = importlib.util.module_from_spec(spec); spec.loader.exec_module(vc)
schema = json.loads((root / "esp32_firmware/config/config.schema.json").read_text())
good = json.loads((root / "esp32_firmware/config/config.example.json").read_text())

def refs(n):
    if isinstance(n, dict):
        for k, v in n.items():
            if k == "$ref": yield v
            else: yield from refs(v)
    elif isinstance(n, list):
        for x in n: yield from refs(x)
for r in set(refs(schema)):
    node = schema
    for p in r.lstrip("#/").split("/"): node = node[p]          # KeyError = broken $ref

failures = 0
def check(label, ok):
    global failures
    print(("PASS " if ok else "FAIL ") + label); failures += 0 if ok else 1

check("example validates", vc.validate(good, schema, schema) == [])
def rejects(mut, expect):
    d = copy.deepcopy(good); mut(d)
    check(f"rejects: {expect}", any(expect in e for e in vc.validate(d, schema, schema)))

rejects(lambda d: d.__setitem__("schemaVersion", 3), "must be 2")
rejects(lambda d: d["devices"][0].__setitem__("id", "bad id"), "does not match")
rejects(lambda d: d["devices"][0].__setitem__("icon", "toaster"), "not in")
rejects(lambda d: d["devices"][0].__setitem__("pinn", 4), "unknown property")
rejects(lambda d: d["mqtt"].__setitem__("homeId", "My/Home"), "does not match")
rejects(lambda d: d["mqtt"].__setitem__("port", 70000), "> 65535")
rejects(lambda d: d["mqtt"].__setitem__("host", "192.168.1.5"), "forbidden form")     # IP address as broker host
rejects(lambda d: d["mqtt"].__setitem__("host", "x"), "does not match")
rejects(lambda d: d["schedules"][0].__setitem__("time", "25:00"), "does not match")
rejects(lambda d: d["schedules"][0].__setitem__("days", []), "fewer than 1")
rejects(lambda d: d["automations"][0]["actions"].extend([d["automations"][0]["actions"][0]] * 4), "more than 4")
rejects(lambda d: d["scenes"][0]["actions"][0].__setitem__("target", "type:pir"), "does not match")
rejects(lambda d: d.pop("schemaVersion"), "missing required")
rejects(lambda d: d["board"].__setitem__("tz", "bad tz"), "does not match")
rejects(lambda d: d["devices"][0].__setitem__("autoOffMin", 5000), "> 1440")
rejects(lambda d: d["devices"][0].__setitem__("bootState", "sometimes"), "not in")
sys.exit(1 if failures else 0)
