#!/usr/bin/env python3
"""Cross-checks the MQTT/REST contract between the firmware (C++) and the app (Dart) by extracting it from both.

 * topic layout and leaf names
 * protocol version
 * every command op / field the app sends is on the firmware's allow-list
 * every JSON key the app reads from inventory/state/ack exists in what the firmware emits
 * every REST path the app calls is a route the firmware registers, with the same HTTP method
Heuristic (regex based): it catches drift, it does not prove the two sides behave identically.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FW = ROOT / "esp32_firmware/esp32_smart_home"
LIB = ROOT / "lib"
read = lambda p: p.read_text(encoding="utf-8")
cloud, snap, api, core = read(FW / "cloud.cpp"), read(FW / "snapshot.cpp"), read(FW / "api.cpp"), read(FW / "core_logic.h")
models, home, esp = read(LIB / "mqtt_models.dart"), read(LIB / "home_controller.dart"), read(LIB / "esp32_api.dart")
svc = read(LIB / "mqtt_service.dart")
problems = []
def need(cond, msg):
    if not cond: problems.append(msg)

# ---- topics
fw_topic = re.search(r'snprintf\(out, cap, "([^"]+)"', core).group(1)
need(fw_topic == "sh/v1/%s/%s/%s", f"firmware topic format changed: {fw_topic}")
need("const String topicRoot = 'sh/v1'" in models.replace("const String topicRoot = 'sh/v1';", "const String topicRoot = 'sh/v1'"), "app topic root differs")
fw_leaves = set(re.findall(r'buildTopic\(t\w+, sizeof\(t\w+\), cfg\.mqtt\.homeId, gBoardId, "(\w+)"\)', cloud))
app_leaves = set(re.search(r"RegExp\(r'\^\((.*?)\)\$'\)", models).group(1).split("|"))
need(fw_leaves == app_leaves, f"topic leaves differ: firmware {sorted(fw_leaves)} vs app {sorted(app_leaves)}")

# ---- protocol version
fw_versions = set(re.findall(r'\["v"\]\s*=\s*(\d+)', cloud + snap)) | set(re.findall(r'\["v"\]\s*\|\s*0\)\s*!=\s*(\d+)', cloud))
app_version = re.search(r"const int protocolVersion = (\d+);", models).group(1)
need(fw_versions == {app_version}, f"protocol version differs: firmware {fw_versions} vs app {app_version}")

# ---- command ops
fw_ops = set(re.findall(r'!strcmp\(op, "([\w.]+)"\)', cloud))
app_ops = set(re.findall(r"'op':\s*'([\w.]+)'", home)) | set(re.findall(r"'op':\s*op\.delete \? '([\w.]+)' : '([\w.]+)'", home) and sum(map(list, re.findall(r"'op':\s*op\.delete \? '([\w.]+)' : '([\w.]+)'", home)), []))
need(app_ops <= fw_ops, f"app sends ops the firmware does not allow: {sorted(app_ops - fw_ops)}")
need(len(app_ops) >= 6, f"could not extract the app's ops (got {sorted(app_ops)})")

# device.update fields the app sends must be handled by the firmware's limited editor
exec_update = cloud[cloud.index("execDeviceUpdate"): cloud.index("static const char* execute")]
fw_update_keys = set(re.findall(r'd\["(\w+)"\]', exec_update))
app_update_keys = set(re.findall(r"if \((?:name|icon|roomId) != null\) '(\w+)'", home))
need(app_update_keys <= fw_update_keys, f"app sends device.update keys the firmware ignores: {sorted(app_update_keys - fw_update_keys)}")
need(not ({"pin", "watts", "invert", "bootState"} & fw_update_keys), "firmware cloud device.update must not accept hardware fields")
# set / dnd / scene argument names
for op, keys in {"set": {"dev", "on"}, "dnd": {"on"}, "scene": {"scene"}}.items():
    need(all(f'"{k}"' in cloud for k in keys), f"firmware does not read {keys} for {op}")
need("'dev': sp.device" in home and "'on': on" in home and "'scene': sceneId" in home, "app does not send set/scene arguments as expected")

# ---- keys the app reads must be emitted by the firmware
emitted = set(re.findall(r'\["(\w+)"\]\s*=', snap + cloud)) | set(re.findall(r'\["(\w+)"\]\.to<', snap)) | set(re.findall(r'(?:doc|o|d|x|r|s|a|counts|p)\["(\w+)"\]', snap + cloud))
def keys_read(src, start, end):
    seg = src[src.index(start): src.index(end)]
    return set(re.findall(r"(?:json|v|r|counts|pins)\['(\w+)'\]", seg))
read_keys = keys_read(models, "class RoomInfo", "// ---------------------------------------------------------------------------- live state") \
          | keys_read(models, "class BoardState", "/// Where a board's data currently comes from.")
# device-level keys (inventory.devices[*]) and state d.<id>.on/wh
read_keys -= {"v"}
missing = sorted(k for k in read_keys if k not in emitted)
need(not missing, f"app reads keys the firmware never emits: {missing}")

# ---- REST paths and methods
fw_routes = set(re.findall(r'server\.on\("(/api/v2/[\w/]+)", HTTP_(GET|POST),', api))
app_calls = set()
for m in re.finditer(r"_call\('(GET|POST)', '([\w/]+)'", esp):   app_calls.add(("/api/v2/" + m.group(2), m.group(1)))
for m in re.finditer(r"_write\(\s*'([\w/]+)'", esp):             app_calls.add(("/api/v2/" + m.group(1), "POST"))
for m in re.finditer(r"http\.call\('(GET|POST)', '([\w/]+)'", read(LIB / "boards_page.dart")): app_calls.add(("/api/v2/" + m.group(2), m.group(1)))
fw_set = {(p, m) for p, m in fw_routes}
need(len(app_calls) >= 12, f"could not extract the app's REST calls (got {len(app_calls)})")
bad = sorted(c for c in app_calls if c not in fw_set)
need(not bad, f"app calls REST routes the firmware does not register (or with another method): {bad}")
need(all(m == "POST" for p, m in fw_routes if re.search(r"/(set|scene|dnd|claim|add|update|delete|upsert|connect|reset|replace|rotate|mqtt|board|ca|remove)$|/wifi/scan$", p) and not (p.endswith("/scan") and m == "GET")),
     "a write route is registered with a non-POST method")

if problems:
    print("PROTOCOL DRIFT:"); [print("  -", p) for p in problems]; sys.exit(1)
print(f"protocol consistent: topic leaves {sorted(fw_leaves)}, version {app_version}, {len(app_ops)} app ops all allow-listed "
      f"({sorted(fw_ops - app_ops)} are firmware-only), {len(read_keys)} JSON keys read by the app all emitted, {len(app_calls)} REST calls all registered")
