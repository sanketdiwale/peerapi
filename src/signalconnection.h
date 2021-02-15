/*
 *  Copyright 2016 The PeerApi Project Authors. All rights reserved.
 *
 *  Ryan Lee
 */

 /*
   Reference: socket.io-client-cpp

   Copyright (c) 2015, Melo Yao
   All rights reserved.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to all conditions.

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
 */


#ifndef __PEERAPI_SIGNAL_H__
#define __PEERAPI_SIGNAL_H__

#include <string>

#if _DEBUG || DEBUG
#include <websocketpp/config/debug_asio.hpp>
#else
#include <websocketpp/config/asio_client.hpp>
#endif //DEBUG

#include "websocketpp/client.hpp"
#include "websocketpp/common/thread.hpp"

#include "rtc_base/third_party/sigslot/sigslot.h"
#include "rtc_base/strings/json.h"

namespace peerapi {

class SignalInterface {
public:
  virtual void Open(const std::string& id, const std::string& password) = 0;
  virtual void Close() = 0;

  virtual void SendCommand(const std::string id,
                           const std::string commandname,
                           const Json::Value& data) = 0;
  virtual void SendGlobalCommand(const std::string commandname,
                           const Json::Value& data) = 0;
  std::string session_id() { return session_id_; }
 
  // sigslots
  sigslot::signal1<const Json::Value&> SignalOnCommandReceived_;
  sigslot::signal1<const websocketpp::close::status::value> SignalOnClosed_;


protected:
  std::string session_id_;
};


class Signal
  : public SignalInterface {
public:
  enum con_state
  {
    con_opening,
    con_opened,
    con_closing,
    con_closed
  };

  using string = std::string;

#if _DEBUG || DEBUG
  typedef websocketpp::config::debug_asio_tls client_config;
#else
  typedef websocketpp::config::asio_tls_client client_config;
#endif //DEBUG
  typedef websocketpp::client<client_config> client_type;

  Signal(const string url);
  ~Signal();

  void Open(const string& id, const string& password);
  void Close();
  void SyncClose();

  void SendCommand(const string channel,
                   const string commandname,
                   const Json::Value& data);
  void SendGlobalCommand(const string commandname,
                         const Json::Value& data);

  void Teardown();

  bool opened() const { return con_state_ == con_opened;}
  void set_reconnect_attempts(unsigned attempts) { reconn_attempts_ = attempts; }
  void set_reconnect_delay(unsigned millis) { reconn_delay_ = millis; if (reconn_delay_max_<millis) reconn_delay_max_ = millis; }
  void set_reconnect_delay_max(unsigned millis) { reconn_delay_max_ = millis; if (reconn_delay_>millis) reconn_delay_ = millis; }


protected:
  void Connect();
  asio::io_service& GetIoService();

private:
  void SendOpenCommand();
  void OnCommandReceived(Json::Value& message);

  void RunLoop();
  void ConnectInternal();
  void CloseInternal(websocketpp::close::status::value const& code, string const& desc);
  void TimeoutReconnect(websocketpp::lib::asio::error_code const& ec);
  unsigned NextDelay() const;

  //websocket callbacks
  void OnFail(websocketpp::connection_hdl con);
  void OnOpen(websocketpp::connection_hdl con);
  void OnClose(websocketpp::connection_hdl con);
  void OnMessage(websocketpp::connection_hdl con, client_type::message_ptr msg);

  void ResetState();

  typedef websocketpp::lib::shared_ptr<asio::ssl::context> context_ptr;
  context_ptr OnTlsInit(websocketpp::connection_hdl con);

  // Connection pointer for client functions.
  websocketpp::connection_hdl con_hdl_;
  client_type client_;

  std::unique_ptr<std::thread> network_thread_;
  std::unique_ptr<websocketpp::lib::asio::steady_timer> reconn_timer_;
  con_state con_state_;

  unsigned reconn_delay_;
  unsigned reconn_delay_max_;
  unsigned reconn_attempts_;
  unsigned reconn_made_;

  // Signal server
  string url_;
  string user_id_;
  string user_password_;
}; // class Signal


} // namespace peerapi

#endif // __PEERAPI_SIGNAL_H__