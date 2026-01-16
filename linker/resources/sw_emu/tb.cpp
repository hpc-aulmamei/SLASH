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
