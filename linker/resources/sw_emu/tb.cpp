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

  while (true) {
    zmq::message_t request;
    socket.recv(request);
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
      socket.recv(data);
      void* buffer = new uint8_t[bufferSize];
      memcpy(buffer, data.data(), bufferSize);

      buffers[name] = buffer;
      bufferSizes[name] = bufferSize;
      socket.send(zmq::message_t("OK", 2), zmq::send_flags::none);
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
    } else if (command == "exit") {
      socket.send(zmq::message_t("OK", 2), zmq::send_flags::none);
      break;
    }
  }

  return 0;
}
