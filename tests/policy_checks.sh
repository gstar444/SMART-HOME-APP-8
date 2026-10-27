#!/bin/sh
# "Never do X" rules, enforced by grep across the shipped sources. POSIX sh, no dependencies.
# (They complement the behavioural tests: a rule here can not be forgotten when someone adds a new file.)
cd "$(dirname "$0")/.." || exit 2
fail=0
check() {  # check <description> <command that prints offending lines>
  out=$(eval "$2" 2>/dev/null)
  if [ -n "$out" ]; then echo "VIOLATION: $1"; echo "$out" | head -5 | sed 's/^/    /'; fail=1; else echo "ok: $1"; fi
}
FW=esp32_firmware/esp32_smart_home
check "firmware never calls setInsecure()"                         "grep -n 'setInsecure' $FW/*.cpp $FW/*.h $FW/*.ino"
check "firmware has no hotspot/default password literal"           "grep -nE '\"12345678\"|AP_PASS|admin123|changeme' $FW/*.cpp $FW/*.h $FW/*.ino"
check "firmware never prints a password, token or token hash VALUE" "grep -nE 'Serial\.(print|printf|println)[^;]*(wifi\.password|mqtt\.password|\.tokenHash|pendPass|\.password)' $FW/*.cpp $FW/*.ino"
check "firmware has no plaintext MQTT port (1883) outside docs"    "grep -nE '\b1883\b' $FW/*.cpp $FW/*.h | grep -v '^.*://' | grep -vE 'validateMqtt|mqtt_port|//'"
check "firmware reads no credential or relay command from the URL query" "grep -nE 'server\.arg\(\"(key|pass|password|ssid|token|on|id)\"\)|server\.hasArg\(\"(key|pass|token)\"\)' $FW/*.cpp"
check "firmware has no HTTP_ANY route (writes are POST only)"      "grep -n 'HTTP_ANY' $FW/*.cpp"
check "firmware has no CORS header"                                "grep -niE 'Access-Control-Allow' $FW/*.cpp"
check "firmware keeps the chip guard (#error for unsupported chips)" "grep -L '#error \"Unsupported chip' $FW/board_profile.h"
check "app never accepts a bad TLS certificate"                    "grep -n 'onBadCertificate' lib/*.dart | grep -v '^[^:]*:[0-9]*: *//' | grep -v '=> false'"
check "app never turns TLS off (secure = false)"                   "grep -nE 'secure *= *false' lib/*.dart"
check "app keeps no API key / token / password in shared_preferences" "grep -nE \"(setString|setStringList)\\([^)]*(apiKey|token|password)\" lib/*.dart"
check "app has no hardcoded broker credentials"                    "grep -nE \"(password|username) *[:=] *'[A-Za-z0-9]{4,}'\" lib/*.dart | grep -vE \"hint|label|'password'|'username'\""
check "Android: no cleartext permission in the manifest or the network config (comments ignored)" "python3 - <<'PY'
import re, xml.dom.minidom as m
for f in ('android/app/src/main/AndroidManifest.xml', 'android/app/src/main/res/xml/network_security_config.xml'):
    d = m.parse(f)
    for el in d.getElementsByTagName('*'):
        for a in ('android:usesCleartextTraffic', 'cleartextTrafficPermitted'):
            if el.getAttribute(a).lower() == 'true': print(f + ': ' + el.tagName + ' ' + a + '=true')
PY"
check "Android: release is never signed with the debug key"        "grep -n 'getByName(\"debug\")' android/app/build.gradle.kts"
check "Android: backup of app data stays disabled"                 "grep -L 'android:allowBackup=\"false\"' android/app/src/main/AndroidManifest.xml"
check "no private key or keystore committed"                       "git ls-files 2>/dev/null | grep -E '\\.(jks|keystore|p12|pem)$' | grep -v 'esp32_firmware/certs/'; find . -name 'key.properties' -not -path './.git/*' | head -3; grep -rln 'BEGIN [A-Z ]*PRIVATE KEY' --include='*' . 2>/dev/null | grep -vE '^\\./(docs|tests|test|tools|esp32_firmware/(esp32_smart_home|tools))' | head -3"
check "CA files are public certificates only"                      "grep -l 'PRIVATE KEY' esp32_firmware/certs/*.pem"
[ $fail -eq 0 ] && echo "policy checks passed"
exit $fail
