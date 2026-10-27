#pragma once
// Functional stand-in for arduino-esp32's WebServer: keeps the route table and lets a test dispatch a request
// through api.cpp's real handlers. Mirrors the real behaviour that matters for security: a request header is only
// visible to the handler if it was registered with collectHeaders(); a body is available as arg("plain").
#include <functional>
#include "Arduino.h"
enum HTTPMethod { HTTP_GET, HTTP_POST };
struct WebServer {
  WebServer(int) {}
  typedef std::function<void(void)> THandlerFunction;
  struct Route { std::string uri; HTTPMethod method; THandlerFunction handler; };
  std::vector<Route> routes;
  THandlerFunction notFound;
  std::vector<std::string> collected;
  // current request
  std::string reqUri; std::map<std::string, std::string> reqHeaders; std::string reqBody;
  // last response
  int status = 0; std::string body; std::map<std::string, std::string> resHeaders;

  void on(const char* uri, HTTPMethod m, THandlerFunction h) { routes.push_back({uri, m, h}); }
  void onNotFound(THandlerFunction h) { notFound = h; }
  void begin() {}
  void handleClient() {}
  void collectHeaders(const char* headerKeys[], const size_t headerKeysCount) {
    collected.clear();
    for (size_t i = 0; i < headerKeysCount; i++) collected.push_back(headerKeys[i]);
  }
  bool hasArg(const char* name) { return std::string(name) == "plain" && !reqBody.empty(); }
  String arg(const char* name) { return std::string(name) == "plain" ? String(reqBody) : String(); }
  String header(const char* name) {
    for (const std::string& c : collected) if (c == name && reqHeaders.count(c)) return String(reqHeaders[c]);
    return String();
  }
  String uri() { return String(reqUri); }
  void sendHeader(const char* k, const char* v) { resHeaders[k] = v; }
  void send(int code, const char* /*type*/, const String& s) { status = code; body = s; }

  // ---- test driver
  void dispatch(HTTPMethod m, const std::string& fullUri, const std::map<std::string, std::string>& headers, const std::string& b) {
    reqUri = fullUri.substr(0, fullUri.find('?'));   // like the real server, the query is not part of uri()
    reqHeaders = headers; reqBody = b; status = 0; body.clear(); resHeaders.clear();
    for (Route& r : routes) if (r.uri == reqUri && r.method == m) { r.handler(); return; }
    if (notFound) notFound();
  }
};
