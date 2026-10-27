// Minimal stand-in for the subset of the ArduinoJson v7 API used by the firmware, with a real JSON
// parser/serializer, so config_store.cpp can run on a PC. It models the documented behaviour; it is
// NOT ArduinoJson and can not prove compatibility with the real library.
#pragma once
#include <memory>
#include <type_traits>
#include "Arduino.h"
#include "LittleFS.h"

namespace sj {
enum Kind { KNull, KBool, KInt, KFloat, KStr, KArr, KObj };
struct Node;
typedef std::shared_ptr<Node> NodeP;
struct Node {
  Kind k = KNull; bool b = false; long long i = 0; double f = 0; std::string s;
  std::vector<NodeP> arr; std::vector<std::pair<std::string, NodeP>> obj;
};
inline NodeP mk(Kind k = KNull) { auto p = std::make_shared<Node>(); p->k = k; return p; }
inline NodeP findKey(const NodeP& o, const std::string& key) {
  if (!o || o->k != KObj) return nullptr;
  for (auto& kv : o->obj) if (kv.first == key) return kv.second;
  return nullptr;
}
}  // namespace sj

class JsonObject; class JsonObjectConst; class JsonArray; class JsonArrayConst; class JsonVariant; class JsonVariantConst;

template <class T> struct is_str : std::false_type {};
template <> struct is_str<const char*> : std::true_type {};
template <> struct is_str<char*> : std::true_type {};
template <> struct is_str<String> : std::true_type {};

class JsonVariantConst {
 public:
  sj::NodeP n;
  JsonVariantConst() {}
  explicit JsonVariantConst(sj::NodeP p) : n(p) {}
  bool isNull() const { return !n || n->k == sj::KNull; }
  size_t size() const { return !n ? 0 : (n->k == sj::KArr ? n->arr.size() : n->obj.size()); }
  template <class T> bool is() const {
    if (!n) return false;
    if constexpr (std::is_same<T, bool>::value) return n->k == sj::KBool;
    else if constexpr (std::is_integral<T>::value) return n->k == sj::KInt;
    else if constexpr (std::is_floating_point<T>::value) return n->k == sj::KInt || n->k == sj::KFloat;
    else if constexpr (is_str<T>::value) return n->k == sj::KStr;
    else if constexpr (std::is_same<T, JsonObject>::value || std::is_same<T, JsonObjectConst>::value) return n->k == sj::KObj;
    else if constexpr (std::is_same<T, JsonArray>::value || std::is_same<T, JsonArrayConst>::value) return n->k == sj::KArr;
    else return !isNull();
  }
  template <class T> T as() const;
  JsonVariantConst operator[](const char* key) const { return JsonVariantConst(sj::findKey(n, key)); }
  template <class T> T operator|(T def) const { return is<T>() ? as<T>() : def; }
};

class JsonObjectConst : public JsonVariantConst {
 public:
  JsonObjectConst() {}
  explicit JsonObjectConst(sj::NodeP p) : JsonVariantConst(p) {}
  JsonObjectConst(const JsonVariantConst& v) : JsonVariantConst(v.n) {}
};

class JsonArrayConst : public JsonVariantConst {
 public:
  JsonArrayConst() {}
  explicit JsonArrayConst(sj::NodeP p) : JsonVariantConst(p) {}
  JsonArrayConst(const JsonVariantConst& v) : JsonVariantConst(v.n) {}
  struct It {
    const std::vector<sj::NodeP>* v; size_t i;
    bool operator!=(const It& o) const { return i != o.i; }
    void operator++() { i++; }
    JsonVariantConst operator*() const { return JsonVariantConst((*v)[i]); }
  };
  static std::vector<sj::NodeP>& empty() { static std::vector<sj::NodeP> e; return e; }
  It begin() const { return It{n && n->k == sj::KArr ? &n->arr : &empty(), 0}; }
  It end() const { return It{n && n->k == sj::KArr ? &n->arr : &empty(), n && n->k == sj::KArr ? n->arr.size() : 0}; }
};

// assignable slot (parent object + key); materialised on assignment
class JsonVariant : public JsonVariantConst {
  sj::NodeP parent_; std::string key_;
  sj::NodeP& ensure() {
    if (!n) { n = sj::mk(); if (parent_) parent_->obj.push_back({key_, n}); }
    return n;
  }
 public:
  JsonVariant() {}
  JsonVariant(sj::NodeP node) : JsonVariantConst(node) {}
  JsonVariant(sj::NodeP parent, const std::string& key) : JsonVariantConst(sj::findKey(parent, key)), parent_(parent), key_(key) {}
  JsonVariant(const JsonVariantConst& v) : JsonVariantConst(v.n) {}
  JsonVariant operator[](const char* key) {
    if (!n && parent_) ensure();
    if (n && n->k != sj::KObj) { *n = sj::Node(); n->k = sj::KObj; }
    if (!n) { ensure(); n->k = sj::KObj; }
    return JsonVariant(n, key);
  }
  template <class T> JsonVariant& operator=(const T& v) {
    sj::NodeP& p = ensure();
    p->arr.clear(); p->obj.clear();
    if constexpr (std::is_same<T, bool>::value) { p->k = sj::KBool; p->b = v; }
    else if constexpr (std::is_integral<T>::value) { p->k = sj::KInt; p->i = (long long)v; }
    else if constexpr (std::is_floating_point<T>::value) { p->k = sj::KFloat; p->f = v; }
    else if constexpr (std::is_convertible<T, std::string>::value) { p->k = sj::KStr; p->s = std::string(v); }
    else static_assert(sizeof(T) == 0, "unsupported assignment");
    return *this;
  }
  JsonVariant& operator=(const char* v) { sj::NodeP& p = ensure(); p->arr.clear(); p->obj.clear(); p->k = sj::KStr; p->s = v ? v : ""; return *this; }
  JsonVariant& operator=(char* v) { return operator=((const char*)v); }
  template <class T> T to();
};

class JsonObject : public JsonObjectConst {
 public:
  JsonObject() {}
  explicit JsonObject(sj::NodeP p) : JsonObjectConst(p) {}
  JsonVariant operator[](const char* key) const { return JsonVariant(n, key); }
  JsonVariantConst operator[](const String& key) const { return JsonVariantConst(sj::findKey(n, key)); }
};

class JsonArray : public JsonArrayConst {
 public:
  JsonArray() {}
  explicit JsonArray(sj::NodeP p) : JsonArrayConst(p) {}
  template <class T> T add() {
    if constexpr (std::is_same<T, JsonObject>::value) { auto c = sj::mk(sj::KObj); n->arr.push_back(c); return JsonObject(c); }
    else { auto c = sj::mk(sj::KArr); n->arr.push_back(c); return JsonArray(c); }
  }
  template <class T> void add(const T& v) {
    auto c = sj::mk();
    JsonVariant slot(c);
    slot = v;
    n->arr.push_back(c);
  }
};

template <class T> T JsonVariantConst::as() const {
  if constexpr (std::is_same<T, bool>::value) return n && n->k == sj::KBool ? n->b : false;
  else if constexpr (std::is_integral<T>::value) return n ? (n->k == sj::KInt ? (T)n->i : (n->k == sj::KFloat ? (T)n->f : (T)0)) : (T)0;
  else if constexpr (std::is_floating_point<T>::value) return n ? (n->k == sj::KInt ? (T)n->i : (T)n->f) : (T)0;
  else if constexpr (std::is_same<T, const char*>::value) return n && n->k == sj::KStr ? n->s.c_str() : nullptr;
  else if constexpr (std::is_same<T, String>::value) return String(n && n->k == sj::KStr ? n->s : std::string());
  else if constexpr (std::is_same<T, JsonObject>::value) return JsonObject(n);
  else if constexpr (std::is_same<T, JsonObjectConst>::value) return JsonObjectConst(n);
  else if constexpr (std::is_same<T, JsonArray>::value) return JsonArray(n);
  else if constexpr (std::is_same<T, JsonArrayConst>::value) return JsonArrayConst(n);
  else return JsonVariantConst(n);
}
template <class T> T JsonVariant::to() {
  ensure();
  n->arr.clear(); n->obj.clear(); n->s.clear();
  if constexpr (std::is_same<T, JsonObject>::value) { n->k = sj::KObj; return JsonObject(n); }
  else { n->k = sj::KArr; return JsonArray(n); }
}

class JsonDocument : public JsonVariant {
 public:
  JsonDocument() : JsonVariant(sj::mk()) {}
  JsonVariant operator[](const char* key) {
    if (n->k != sj::KObj) { n->k = sj::KObj; }
    return JsonVariant(n, key);
  }
  template <class T> T to() { n->arr.clear(); n->obj.clear(); if constexpr (std::is_same<T, JsonObject>::value) { n->k = sj::KObj; return JsonObject(n); } else { n->k = sj::KArr; return JsonArray(n); } }
};

struct DeserializationError {
  const char* msg;
  explicit operator bool() const { return msg != nullptr; }
  const char* c_str() const { return msg ? msg : "Ok"; }
};

namespace sj {
struct Parser {
  const char* p; const char* e; const char* err = nullptr;
  void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
  bool lit(const char* s) { size_t n = strlen(s); if ((size_t)(e - p) >= n && !strncmp(p, s, n)) { p += n; return true; } return false; }
  std::string str() {
    std::string out; p++;
    while (p < e && *p != '"') {
      if (*p == '\\' && p + 1 < e) {
        p++;
        switch (*p) { case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
          case 'u': { unsigned cp = (unsigned)strtoul(std::string(p + 1, 4).c_str(), nullptr, 16); p += 4;
            if (cp < 0x80) out += (char)cp; else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 63)); }
            else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 63)); out += (char)(0x80 | (cp & 63)); } break; }
          default: out += *p; }
        p++;
      } else out += *p++;
    }
    if (p >= e) err = "IncompleteInput"; else p++;
    return out;
  }
  NodeP val(int depth = 0) {
    ws();
    if (p >= e || depth > 20) { err = "InvalidInput"; return mk(); }
    if (*p == '{') {
      auto o = mk(KObj); p++; ws();
      if (p < e && *p == '}') { p++; return o; }
      while (!err) {
        ws(); if (p >= e || *p != '"') { err = "InvalidInput"; break; }
        std::string k = str(); ws(); if (p >= e || *p != ':') { err = "InvalidInput"; break; } p++;
        o->obj.push_back({k, val(depth + 1)}); ws();
        if (p < e && *p == ',') { p++; continue; }
        if (p < e && *p == '}') { p++; break; }
        err = "InvalidInput";
      }
      return o;
    }
    if (*p == '[') {
      auto a = mk(KArr); p++; ws();
      if (p < e && *p == ']') { p++; return a; }
      while (!err) {
        a->arr.push_back(val(depth + 1)); ws();
        if (p < e && *p == ',') { p++; continue; }
        if (p < e && *p == ']') { p++; break; }
        err = "InvalidInput";
      }
      return a;
    }
    if (*p == '"') { auto s = mk(KStr); s->s = str(); return s; }
    if (lit("true")) { auto b = mk(KBool); b->b = true; return b; }
    if (lit("false")) { auto b = mk(KBool); return b; }
    if (lit("null")) return mk();
    const char* st = p; bool fl = false;
    while (p < e && (isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E')) { if (*p == '.' || *p == 'e' || *p == 'E') fl = true; p++; }
    if (st == p) { err = "InvalidInput"; return mk(); }
    std::string t(st, p);
    if (fl) { auto f = mk(KFloat); f->f = atof(t.c_str()); return f; }
    auto i = mk(KInt); i->i = atoll(t.c_str()); return i;
  }
};
inline void ser(const NodeP& n, std::string& o) {
  if (!n) { o += "null"; return; }
  switch (n->k) {
    case KNull: o += "null"; break;
    case KBool: o += n->b ? "true" : "false"; break;
    case KInt: o += std::to_string(n->i); break;
    case KFloat: { char b[40]; snprintf(b, sizeof(b), "%.6g", n->f); o += b; break; }
    case KStr: o += '"'; for (char c : n->s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; } else o += c; } o += '"'; break;
    case KArr: o += '['; for (size_t i = 0; i < n->arr.size(); i++) { if (i) o += ','; ser(n->arr[i], o); } o += ']'; break;
    case KObj: o += '{'; for (size_t i = 0; i < n->obj.size(); i++) { if (i) o += ','; o += '"' + n->obj[i].first + "\":"; ser(n->obj[i].second, o); } o += '}'; break;
  }
}
}  // namespace sj

inline DeserializationError deserializeJson(JsonDocument& d, const char* in, size_t n) {
  sj::Parser ps{in, in + n};
  sj::NodeP r = ps.val();
  ps.ws();
  if (!ps.err && ps.p != ps.e) ps.err = "InvalidInput";
  if (ps.err) return {ps.err};
  *d.n = *r;
  return {nullptr};
}
inline DeserializationError deserializeJson(JsonDocument& d, const String& s) { return deserializeJson(d, s.c_str(), s.size()); }
inline DeserializationError deserializeJson(JsonDocument& d, File& f) { std::string s = f.contents(); return deserializeJson(d, s.c_str(), s.size()); }

inline size_t serializeJson(const JsonDocument& d, String& out) { std::string s; sj::ser(d.n, s); out = String(s); return s.size(); }
inline size_t serializeJson(const JsonDocument& d, File& f) { std::string s; sj::ser(d.n, s); f.print(s.c_str()); return s.size(); }
