/**************************************************************************/
/*                                                                        */
/*                          WWIV Version 5.x                              */
/*             Copyright (C)2015-2022, WWIV Software Services             */
/*                                                                        */
/*    Licensed  under the  Apache License, Version  2.0 (the "License");  */
/*    you may not use this  file  except in compliance with the License.  */
/*    You may obtain a copy of the License at                             */
/*                                                                        */
/*                http://www.apache.org/licenses/LICENSE-2.0              */
/*                                                                        */
/*    Unless  required  by  applicable  law  or agreed to  in  writing,   */
/*    software  distributed  under  the  License  is  distributed on an   */
/*    "AS IS"  BASIS, WITHOUT  WARRANTIES  OR  CONDITIONS OF ANY  KIND,   */
/*    either  express  or implied.  See  the  License for  the specific   */
/*    language governing permissions and limitations under the License.   */
/**************************************************************************/
#include "binkp/fake_connection.h"

#include "core/os.h"
#include "core/scope_exit.h"
#include "core/socket_exceptions.h"
#include "core/strings.h"
#include "binkp/binkp_commands.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

using namespace std::chrono;
using namespace wwiv::core;
using namespace wwiv::os;
using namespace wwiv::strings;
using namespace wwiv::net;

FakeBinkpPacket::FakeBinkpPacket(const void* data, int size) {
  auto p = static_cast<const char*>(data);
  header_ = *p++ << 8;
  header_ = header_ | *p++;
  is_command_ = (header_ | 0x8000) != 0;
  header_ &= 0x7fff;

  if (is_command_) {
    command_ = *p;
  }
  // size doesn't include the uint16_t header.
  data_ = std::string(p, size - 2);  
}

FakeBinkpPacket::~FakeBinkpPacket() = default;

FakeBinkpPacket::FakeBinkpPacket(const FakeBinkpPacket& o)
    : is_command_(o.is_command_), command_(o.command_), header_(o.header_), data_(o.data_) {}


std::string FakeBinkpPacket::debug_string() const {
  // since data_ doesn't have a trailing nullptr, use stringstream.
  std::stringstream ss;
  if (is_command_) {
    const auto s = !data_.empty() ? data_.substr(1) : data_;
    ss << "[" << BinkpCommands::command_id_to_name(command_) << "] data ='" << s << "'";
  } else {
    ss << "[DATA] data = '" << data_ << "'";
  }
  return ss.str();
}

FakeConnection::FakeConnection() = default;
FakeConnection::~FakeConnection() = default;

uint16_t FakeConnection::read_uint16(std::chrono::duration<double> d) {
  auto predicate = [&]() { 
    std::lock_guard<std::mutex> lock(mu_);
    return !receive_queue_.empty();
  };
  if (!wait_for(predicate, d)) {
    throw timeout_error("timedout on read_uint16");
  }
  std::lock_guard<std::mutex> lock(mu_);
  const auto& packet = receive_queue_.front();
  auto header = packet.header();
  if (packet.is_command()) {
    header |= 0x8000;
  }
  return header;
}

uint8_t FakeConnection::read_uint8(std::chrono::duration<double> d) {
  auto predicate = [&]() { 
    std::lock_guard<std::mutex> lock(mu_);
    return !receive_queue_.empty();
  };
  if (!wait_for(predicate, d)) {
    throw timeout_error("timedout on read_uint8");
  }

  std::lock_guard<std::mutex> lock(mu_);
  const FakeBinkpPacket& front = receive_queue_.front();
  if (!front.is_command()) {
    throw std::logic_error("called read_uint8 on a data packet");
  }
  return front.command();
}

int FakeConnection::receive(void* data, int size, duration<double> d) {
  std::string s = receive(size, d);
  memcpy(data, s.data(), size);
  return size;
}

std::string FakeConnection::receive(int, duration<double> d) {
  auto predicate = [&]() {
    std::lock_guard<std::mutex> lock(mu_);
    return !receive_queue_.empty();
  };
  if (!wait_for(predicate, d)) {
    throw timeout_error("timedout on receive");
  }

  std::lock_guard<std::mutex> lock(mu_);
  wwiv::core::ScopeExit on_exit([=] { receive_queue_.pop(); });
  const FakeBinkpPacket& front = receive_queue_.front();
  return front.data();
}

int FakeConnection::send(const void* data, int size, std::chrono::duration<double>) {
  std::lock_guard<std::mutex> lock(mu_);
  send_queue_.push(FakeBinkpPacket(data, size));
  return size;
}


int FakeConnection::send(const std::string& s, std::chrono::duration<double> d) {
  return send(s.data(), s.length(), d);
}

bool FakeConnection::has_sent_packets() const {
  std::lock_guard<std::mutex> lock(mu_);
  return !send_queue_.empty();
}

FakeBinkpPacket FakeConnection::GetNextPacket() {
  std::lock_guard<std::mutex> lock(mu_);
  if (send_queue_.empty()) {
    throw std::logic_error("GetNextPacket called on empty queue.");
  }
  FakeBinkpPacket packet = send_queue_.front();
  send_queue_.pop();
  return packet;
}

// Reply to the BinkP with a command.
void FakeConnection::ReplyCommand(int8_t command_id, const std::string& data) {
  const int size = 3u + data.size(); /* header + command + data + null*/
  std::unique_ptr<char[]> packet(new char[size]);
  // Actual packet size parameter does not include the size parameter itself.
  // And for sending a command this will be 2 less than our actual packet size.
  const uint16_t packet_length = static_cast<uint16_t>(data.size() + sizeof(uint8_t)) | 0x8000;
  const uint8_t b0 = ((packet_length & 0xff00) >> 8) | 0x80;
  const uint8_t b1 = packet_length & 0x00ff;

  auto* p = packet.get();
  *p++ = b0;
  *p++ = b1;
  *p++ = command_id;
  memcpy(p, data.data(), data.size());  // NOLINT(bugprone-not-null-terminated-result)

  std::lock_guard<std::mutex> lock(mu_);
  receive_queue_.push(FakeBinkpPacket(packet.get(), size));
}

bool FakeConnection::is_open() const { return open_; }
bool FakeConnection::close() { open_ = false; return true; }