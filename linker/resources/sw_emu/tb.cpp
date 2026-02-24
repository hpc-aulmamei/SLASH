/**
 * The MIT License (MIT)
 * Copyright (c) 2025-2026 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <iostream>
#include <ap_fixed.h>
#include <hls_stream.h>
#include <ap_int.h>
#include <zmq.hpp>
#include <json/json.h>
#include <cstdint>
#include <map>
#include <vector>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <fstream>
#include <functional>
#include <thread>
#include <chrono>
#include <cstdlib>

{% for p in prototypes %}
{{ p }}
{% endfor %}

template <typename T>
void assignValue(T& var, const Json::Value& value) {
  if (value.isString()) { std::istringstream iss(value.asString()); iss >> var; }
  else if (value.isInt()) var = static_cast<T>(value.asInt());
  else if (value.isUInt()) var = static_cast<T>(value.asUInt());
  else if (value.isDouble()) var = static_cast<T>(value.asDouble());
  else throw std::runtime_error("Unsupported JSON value type");
}

template <typename T>
Json::Value createJsonValue(const T& var) {
  uint32_t raw = 0;
  const size_t n = sizeof(raw) < sizeof(T) ? sizeof(raw) : sizeof(T);
  std::memcpy(&raw, &var, n);
  return Json::Value(raw);
}

Json::Value createJsonBuffer(const uint8_t* buffer, size_t size) {
  Json::Value value(Json::arrayValue);
  for (size_t i = 0; i < size; ++i) {
    value.append(buffer[i]);
  }
  return value;
}

int main() {
  zmq::context_t context(1);
  zmq::socket_t socket(context, ZMQ_REP);
  socket.bind("tcp://*:5555");

  std::map<std::string, void*> buffers;
  std::map<std::string, size_t> bufferSizes;

{% for v in vars %}
  {{ v }};
{% endfor %}

{% for w in wires %}
  hls::stream<{{ w.ctype }}> {{ w.name }};
{% endfor %}

  std::map<std::string, std::function<void()>> autostartRegistry;
{% for ac in autostart_calls %}
  autostartRegistry["{{ ac.inst }}"] = [&]() {
    {{ ac.top }}({{ ac.call_args | join(", ") }});
  };
{% endfor %}

  std::map<std::string, std::function<Json::Value()>> fetchScalarRegistry;
{% for sym in fetch_scalar_var_symbols %}
  fetchScalarRegistry["{{ sym }}"] = [&]() {
    return createJsonValue({{ sym }});
  };
{% endfor %}

  Json::Value emuManifest;
  bool emuManifestLoaded = false;
  {
    std::ifstream manifestFile("emu_manifest.json");
    if (manifestFile.is_open()) {
      Json::Reader manifestReader;
      emuManifestLoaded = manifestReader.parse(manifestFile, emuManifest);
    }
  }

  bool autostartManifestHadKernels = false;
  bool launchedAutostartThreads = false;
  if (emuManifestLoaded && emuManifest.isObject()) {
    const Json::Value kernels = emuManifest["kernels"];
    if (kernels.isArray()) {
      autostartManifestHadKernels = true;
      for (const auto& k : kernels) {
        if (!k.isObject()) continue;
        if (!k.get("autostart", false).asBool()) continue;
        std::string instance = k.get("instance", "").asString();
        auto it = autostartRegistry.find(instance);
        if (it == autostartRegistry.end()) continue;
        launchedAutostartThreads = true;
        std::thread(it->second).detach();
      }
    }
  }

  if (!autostartManifestHadKernels) {
{% for ac in autostart_calls %}
    // Fallback for older outputs or missing manifest.
    launchedAutostartThreads = true;
    std::thread([&]() {
      {{ ac.top }}({{ ac.call_args | join(", ") }});
    }).detach();
{% endfor %}
  }

  while (true) {
    zmq::message_t request;
    if (!socket.recv(request, zmq::recv_flags::none)) {
      continue;
    }
    std::string req_str(static_cast<char*>(request.data()), request.size());
    Json::Value root;
    Json::Reader reader;
    reader.parse(req_str, root);

    std::string command = root["command"].asString();
    std::string argType;

    if (command == "populate") {
      std::string name = root["name"].asString();
      size_t bufferSize = root["size"].asUInt64();

      zmq::message_t data;
      if (!socket.recv(data, zmq::recv_flags::none)) {
        socket.send(zmq::message_t("ERR", 3), zmq::send_flags::none);
        continue;
      }
      void* buffer = new uint8_t[bufferSize];
      memcpy(buffer, data.data(), bufferSize);

      buffers[name] = buffer;
      bufferSizes[name] = bufferSize;
      socket.send(zmq::message_t("OK", 2), zmq::send_flags::none);
    } else if (command == "stream_in") {
      std::string name = root["name"].asString();
      zmq::message_t data;
      if (!socket.recv(data, zmq::recv_flags::none)) {
        socket.send(zmq::message_t("ERR", 3), zmq::send_flags::none);
        continue;
      }
      bool handled = false;

{% for s in stream_routes %}
{% for alias in s.names %}
      if (!handled && name == "{{ alias }}") {
        handled = true;
        for (size_t i = 0; i < data.size() / sizeof({{ s.ctype }}); i++) {
          {{ s.ctype }} value;
          memcpy(&value, static_cast<uint8_t*>(data.data()) + i * sizeof({{ s.ctype }}), sizeof({{ s.ctype }}));
          {{ s.wire }}.write(value);
        }
      }
{% endfor %}
{% endfor %}

      socket.send(zmq::message_t("OK", 2), zmq::send_flags::none);
    } else if (command == "stream_out") {
      std::string name = root["name"].asString();
      size_t size = root["size"].asUInt64();
      std::vector<uint8_t> buffer(size, 0);
      bool handled = false;

{% for s in stream_routes %}
{% for alias in s.names %}
      if (!handled && name == "{{ alias }}") {
        handled = true;
        for (size_t i = 0; i < size / sizeof({{ s.ctype }}); i++) {
          {{ s.ctype }} value = {{ s.wire }}.read();
          memcpy(buffer.data() + i * sizeof({{ s.ctype }}), &value, sizeof({{ s.ctype }}));
        }
      }
{% endfor %}
{% endfor %}

      socket.send(zmq::message_t(buffer.data(), buffer.size()), zmq::send_flags::none);
    } else if (command == "call") {
      std::string functionName = root["function"].asString();

{% for fc in function_calls %}
      if (functionName == "{{ fc.inst }}") {
{% for line in fc.decode_blocks %}
        {{ line }}
{% endfor %}
        {{ fc.top }}({{ fc.call_args | join(", ") }});
      }
{% endfor %}

      socket.send(zmq::message_t("OK", 2), zmq::send_flags::none);
    } else if (command == "fetch") {
      std::string type = root["type"].asString();
      Json::Value response;

      if (type == "scalar") {
        std::string functionName = root["function"].asString();
        std::string arg = root["arg"].asString();
        bool handledScalar = false;

        if (emuManifestLoaded && emuManifest.isObject()) {
          const Json::Value fetchRoutes = emuManifest["fetch"]["scalar"];
          if (fetchRoutes.isArray()) {
            for (const auto& route : fetchRoutes) {
              if (!route.isObject()) continue;
              if (route.get("function", "").asString() != functionName) continue;
              if (route.get("arg", "").asString() != arg) continue;

              std::string kind = route.get("kind", "").asString();
              if (kind == "var") {
                std::string symbol = route.get("var_symbol", "").asString();
                auto it = fetchScalarRegistry.find(symbol);
                if (it != fetchScalarRegistry.end()) {
                  response = it->second();
                  handledScalar = true;
                  break;
                }
              } else if (kind == "const_u32") {
                response = Json::Value(static_cast<Json::UInt>(route.get("value", 0).asUInt()));
                handledScalar = true;
                break;
              }
            }
          }
        }

        if (!handledScalar) {
{% for fc in fetch_scalar_cases %}
        if (functionName == "{{ fc.inst }}" && arg == "{{ fc.arg }}") {
{% if fc.kind == "var" %}
          response = createJsonValue({{ fc.var }});
{% elif fc.kind == "const_u32" %}
          response = Json::Value(static_cast<Json::UInt>({{ fc.value }}));
{% endif %}
        }
{% endfor %}
        }
      } else if (type == "buffer") {
        std::string name = root["name"].asString();
        if (buffers.find(name) != buffers.end()) {
          response = createJsonBuffer(static_cast<uint8_t*>(buffers[name]), bufferSizes[name]);
        }
      }

      std::string responseStr = Json::writeString(Json::StreamWriterBuilder(), response);
      socket.send(zmq::message_t(responseStr.c_str(), responseStr.size()), zmq::send_flags::none);
    } else if (command == "exit") {
      socket.send(zmq::message_t("OK", 2), zmq::send_flags::none);
      if (launchedAutostartThreads) {
        // Free-running autostart kernels (e.g. ap_ctrl_none stream-only kernels)
        // run in detached threads and may never return. Exiting main() would destroy
        // local hls::stream objects while those threads are still active.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::_Exit(0);
      }
      break;
    } else {
      socket.send(zmq::message_t("ERR", 3), zmq::send_flags::none);
    }
  }

  return 0;
}
