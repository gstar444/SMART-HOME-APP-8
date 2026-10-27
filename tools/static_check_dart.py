#!/usr/bin/env python3
"""Poor man's static checks for the Dart sources when no Dart SDK is available.

NOT a substitute for `flutter analyze`. It catches: unbalanced brackets, imports that do not
resolve, unused project imports, calls to members that do not exist on HomeController/LocalBoard,
undefined Tr strings, and PascalCase type names that are declared nowhere (reviewed by hand).
Usage: python3 tools/static_check_dart.py   (run from the project root)
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
LIB, TEST = ROOT / "lib", ROOT / "test"
problems = []

def strip(src):
    """remove comments and string literals (keeps line structure roughly)"""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            while i < n and src[i] != "\n": i += 1
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2); i = n if j < 0 else j + 2
        elif src.startswith("'''", i) or src.startswith('"""', i):
            q = src[i:i+3]; j = src.find(q, i + 3); i = n if j < 0 else j + 3; out.append('""')
        elif c in "'\"":
            raw = i > 0 and src[i-1] == 'r'
            j = i + 1
            while j < n and src[j] != c:
                if src[j] == "\\" and not raw: j += 1
                if src[j] == "\n": break
                j += 1
            i = j + 1; out.append('""')
        else:
            out.append(c); i += 1
    return "".join(out)

def files():
    return sorted(LIB.glob("*.dart")) + sorted(TEST.glob("*.dart"))

# ---- declarations per lib file
decl = {}
for f in LIB.glob("*.dart"):
    s = strip(f.read_text(encoding="utf-8"))
    names = set(re.findall(r"^(?:abstract\s+|final\s+|sealed\s+|base\s+|interface\s+)*(?:class|enum|mixin|extension type|typedef)\s+(\w+)", s, re.M))
    names |= set(re.findall(r"^extension\s+(\w+)\s+on", s, re.M))
    names |= set(re.findall(r"^(?:const|final)\s+[\w<>?, ]+\s+(\w+)\s*=", s, re.M))
    names |= set(re.findall(r"^(?:[\w<>?,() ]+)\s+(\w+)\(", s, re.M) if False else [])
    names |= set(re.findall(r"^(?:Future<[\w<>?, ]+>|[\w<>?]+)\s+(\w+)\(", s, re.M))
    decl[f.name] = names

# ---- 1. brackets
for f in files():
    s = strip(f.read_text(encoding="utf-8"))
    for o, c in ("()", "[]", "{}"):
        if s.count(o) != s.count(c):
            problems.append(f"{f.name}: unbalanced {o}{c}  ({s.count(o)} vs {s.count(c)})")

# ---- 2. imports resolve + 3. unused project imports
for f in files():
    raw = f.read_text(encoding="utf-8")
    body = strip(raw)
    for m in re.finditer(r"^import\s+'([^']+)'", raw, re.M):
        target = m.group(1)
        if target.startswith("package:esp32_controller/"):
            name = target.split("/", 1)[1]
        elif target.startswith(("dart:", "package:")):
            continue
        else:
            name = target
        p = LIB / name
        if not p.exists():
            problems.append(f"{f.name}: import '{target}' does not resolve"); continue
        used = any(re.search(r"\b" + re.escape(n) + r"\b", body) for n in decl[name])
        if not used and name != "strings_cloud.dart":
            problems.append(f"{f.name}: project import '{name}' looks unused")
        if name == "strings_cloud.dart" and not re.search(r"\b(?:tr|_tr|t)\.\w*(?:cloud|board|Board|local|remote|cap|errNot|errRate|camera|recovery|sourceLabel|providerName|Cloud)\w*", body, re.I):
            problems.append(f"{f.name}: strings_cloud.dart import looks unused")

# ---- 4. Tr getters
tr_defs = set()
for fn in ("strings.dart", "strings_cloud.dart"):
    t = (LIB / fn).read_text(encoding="utf-8")
    tr_defs |= set(re.findall(r"(?:String|bool|List<String>) get (\w+)", t)) | set(re.findall(r"\bString (\w+)\(", t)) | {"hi", "of"}
for f in LIB.glob("*.dart"):
    if f.name.startswith("strings"): continue
    for m in re.finditer(r"\b(?:tr|_tr)\.(\w+)", strip(f.read_text(encoding="utf-8"))):
        if m.group(1) not in tr_defs: problems.append(f"{f.name}: undefined Tr member '{m.group(1)}'")

# ---- 5. member existence on the controllers
def public_members(file, cls):
    s = strip((LIB / file).read_text(encoding="utf-8"))
    i = s.index(f"class {cls}") if f"class {cls}" in s else s.index(f"interface class {cls}")
    # naive: take until the next top-level 'class ' declaration
    j = s.find("\nclass ", i + 10); body = s[i: j if j > 0 else len(s)]
    names = set(re.findall(r"\b(?:get|set)\s+([a-z]\w*)", body))
    names |= set(re.findall(r"^\s{2}(?:@override\s+)?(?:static\s+)?(?:Future<[^>]*>|Future<[\w<>?, ]+>|[\w<>?,() ]+?)\s+([a-z]\w*)\s*\(", body, re.M))
    names |= set(re.findall(r"^\s{2}(?:final|late final|late)?\s*[\w<>?, ]+\s+([a-z]\w*)\s*(?:=|;)", body, re.M))
    names |= set(re.findall(r"final\s+[\w<>?, ]+\s+([a-z]\w*)\s*;", body))
    names |= set(re.findall(r"this\.([a-z]\w*)", body))
    return {n for n in names if not n.startswith("_")}
home = public_members("home_controller.dart", "HomeController") | {"addListener", "removeListener", "dispose", "notifyListeners", "hasListeners"}
local = public_members("esp32_api.dart", "LocalBoard") | {"addListener", "removeListener"}
dash = strip((LIB / "dashboard_page.dart").read_text(encoding="utf-8"))
for m in re.finditer(r"\b_esp\.(\w+)", dash):
    if m.group(1) not in home | {"local", "mqtt", "settings"}: problems.append(f"dashboard_page.dart: _esp.{m.group(1)} is not a HomeController member")
for fn in ("wifi_page.dart", "settings_page.dart"):
    s = strip((LIB / fn).read_text(encoding="utf-8"))
    for m in re.finditer(r"\b(?:_esp|widget\.esp|esp)\.(\w+)", s):
        if m.group(1) not in local: problems.append(f"{fn}: esp.{m.group(1)} is not a LocalBoard member")

# ---- 6. unknown PascalCase types (report; reviewed by hand)
known_sdk = set("""String int double num bool List Map Set Future Stream Object Duration DateTime Timer Completer StreamController StreamSubscription
Uri Random Exception Error FormatException TimeoutException Iterable Function Type Null Never dynamic void Record Symbol RegExp Match Pattern
File Directory HttpClient HttpServer HttpRequest HttpHeaders HttpException SocketException TlsException HandshakeException ContentType InternetAddress SecurityContext
Widget StatelessWidget StatefulWidget State BuildContext Key Scaffold AppBar Text Icon Icons IconButton ListView ListTile Column Row Padding EdgeInsets
SizedBox Card Colors Theme ThemeData ThemeMode MaterialApp MaterialPageRoute Navigator ScaffoldMessenger SnackBar AlertDialog TextButton FilledButton OutlinedButton
TextField TextEditingController InputDecoration TextInputType TextCapitalization DropdownButtonFormField DropdownMenuItem CircularProgressIndicator Center Stack Positioned
IgnorePointer Wrap FloatingActionButton PopupMenuButton PopupMenuItem SimpleDialog SimpleDialogOption SingleChildScrollView CrossAxisAlignment MainAxisSize
VisualDensity Brightness Color AppLifecycleState WidgetsBinding WidgetsBindingObserver ChangeNotifier Listenable ListenableBuilder ValueNotifier Matcher
MultiProvider ChangeNotifierProvider Provider SharedPreferences FlutterSecureStorage MqttServerClient MqttClient MqttConnectMessage MqttConnectionState
MqttConnectReturnCode MqttClientPayloadBuilder MqttPublishPayload MqttPublishMessage MqttReceivedMessage MqttMessage MqttQos NoConnectionException
WebViewController WebViewWidget JavaScriptMode NavigationDelegate SegmentedButton ButtonSegment Slider Switch SwitchListTile Divider Spacer Expanded
Flexible Container BoxDecoration BorderRadius Alignment Align GestureDetector InkWell Material Opacity AnimatedContainer AnimatedSwitcher Tooltip TabBar Tab
TextStyle FontWeight TextAlign Visibility Builder LayoutBuilder MediaQuery SafeArea RefreshIndicator CustomScrollView SliverToBoxAdapter SliverGrid
SliverGridDelegateWithMaxCrossAxisExtent SliverList SliverChildBuilderDelegate SliverChildListDelegate SliverPadding SliverAppBar
TestWidgetsFlutterBinding AssertionError ArgumentError StateError UnimplementedError UnsupportedError FlutterError
Equatable TickerProviderStateMixin SingleTickerProviderStateMixin AnimationController Curves Interval CurvedAnimation FadeTransition ScaleTransition
Offset Size Rect Radius Border BorderSide Shadow BoxShadow Gradient LinearGradient ImageFilter ImageFilter BackdropFilter ClipRRect Image AssetImage
FocusScope FocusNode SystemChrome SystemUiOverlayStyle HapticFeedback MethodChannel PlatformException Clipboard ClipboardData
Radio RadioListTile RadioGroup Checkbox CheckboxListTile Chip ChoiceChip FilterChip ActionChip InputChip RangeSlider Dismissible Draggable DragTarget
Hero Semantics ExcludeSemantics MergeSemantics Banner BottomSheet showModalBottomSheet ModalBottomSheetRoute DraggableScrollableSheet
IconData IconTheme DefaultTextStyle TextTheme ColorScheme MaterialColor MaterialStateProperty WidgetStateProperty WidgetState ButtonStyle
CircleAvatar Drawer DrawerHeader NavigationBar NavigationDestination NavigationRail BottomNavigationBar BottomNavigationBarItem TabBarView TabController DefaultTabController
UniqueKey ValueKey GlobalKey PageStorageKey ObjectKey LocalKey ScrollController ScrollPhysics ClampingScrollPhysics BouncingScrollPhysics PageController PageView
""".split())
all_decl = set().union(*decl.values())
for f in files():
    s = strip(f.read_text(encoding="utf-8"))
    own = set(re.findall(r"\b(?:class|enum|mixin|typedef)\s+(\w+)", s))
    unknown = set()
    for m in re.finditer(r"(?<![\w.$'])([A-Z]\w+)\b", s):
        n = m.group(1)
        if n in all_decl or n in own or n in known_sdk or n.isupper() or len(n) < 3: continue
        unknown.add(n)
    unknown -= {n for n in unknown if n.startswith(("Esp32", "Wifi", "Test"))}
    if unknown: print(f"[review] {f.name}: unfamiliar type names: {sorted(unknown)}")

if problems:
    print("PROBLEMS:"); [print("  -", p) for p in problems]; sys.exit(1)
print("static checks passed (brackets, imports, unused project imports, Tr strings, controller members)")
