/*
*  Copyright 2016 The PeerApi Project Authors. All rights reserved.
*
*  Ryan Lee
*/

#include "control.h"
#include "peer.h"
// #include "api/test/fakeconstraints.h"
#include "pc/test/mock_peer_connection_observers.h"

#include "logging.h"

namespace peerapi {

//
// class PeerControl
//

PeerControl::PeerControl(const string local_id,
                         const string remote_id,
                         PeerObserver* observer,
                         rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
                             peer_connection_factory)
    : local_id_(local_id),
      remote_id_(remote_id),
      control_(observer),
      peer_connection_factory_(peer_connection_factory),
      state_(pClosed) {

}

PeerControl::~PeerControl() {
  RTC_DCHECK(state_ == pClosed);
  DeletePeerConnection();
  LOG_F( INFO ) << "Done";
}


bool PeerControl::Initialize() {

  if (!CreatePeerConnection()) {
    LOG_F(LS_ERROR) << "CreatePeerConnection failed";
    DeletePeerConnection();
    return false;
  }

  webrtc::DataChannelInit init;
  const string data_channel_name = string("peer_data_") + remote_id_;
  if (!CreateDataChannel(data_channel_name, init)) {
    LOG_F(LS_ERROR) << "CreateDataChannel failed";
    DeletePeerConnection();
    return false;
  }

  LOG_F( INFO ) << "Done";
  return true;
}

bool PeerControl::Send(const char* buffer, const size_t size) {
  RTC_DCHECK( state_ == pOpen );
  
  if ( state_ != pOpen ) {
    LOG_F( WARNING ) << "Send data when a peer state is not opened";
    return false;
  }

  return local_data_channel_->Send(buffer, size);
}

bool PeerControl::SyncSend(const char* buffer, const size_t size) {
  RTC_DCHECK( state_ == pOpen );

  if ( state_ != pOpen ) {
    LOG_F( WARNING ) << "Send data when a peer state is not opened";
    return false;
  }

  return local_data_channel_->SyncSend(buffer, size);
}

bool PeerControl::IsWritable() {

  if ( state_ != pOpen ) {
    LOG_F( WARNING ) << "A function was called when a peer state is not opened";
    return false;
  }

  return local_data_channel_->IsWritable();
}

void PeerControl::Close(const CloseCode code) {
//  LOG_F_IF(state_ != pOpen, WARNING) << "Closing peer when it is not opened";

  if ( state_ == pClosing || state_ == pClosed ) {
    LOG_F( WARNING ) << "Close peer when is closing or already closed";
    return;
  }

  state_ = pClosing;

  LOG_F( INFO ) << "Close data-channel of remote_id_ " << remote_id_;

  if ( peer_connection_ ) {

    peer_connection_ = nullptr;

    // As entering here, we can make sure that
    //  - PeerDataChannelObserver::OnStateChange() had been called with kClosed
    //  - PeerControl::OnIceConnectionChange() will be ignored,
    //                 ether kIceConnectionClosed and kIceConnectionDisconnected.
    //    That's because we didn't call peer_connection_->Close().
  
  }

  state_ = pClosed;
  control_->OnPeerClose(remote_id_, code);

}


void PeerControl::CreateOffer(const webrtc::MediaConstraintsInterface* constraints) {
  RTC_DCHECK( state_ == pClosed );

  state_ = pConnecting;
  peer_connection_->CreateOffer(this, constraints);
  LOG_F( INFO ) << "Done";
}


void PeerControl::CreateAnswer(const webrtc::MediaConstraintsInterface* constraints) {
  RTC_DCHECK( state_ == pClosed );

  state_ = pConnecting;
  peer_connection_->CreateAnswer(this, constraints);
  LOG_F( INFO ) << "Done";
}


void PeerControl::ReceiveOfferSdp(const string& sdp) {
  RTC_DCHECK( state_ == pClosed);
  SetRemoteDescription(webrtc::SessionDescriptionInterface::kOffer, sdp);
  CreateAnswer(NULL);
  LOG_F( INFO ) << "Done";
}


void PeerControl::ReceiveAnswerSdp(const string& sdp) {
  RTC_DCHECK( state_ == pConnecting );
  SetRemoteDescription(webrtc::SessionDescriptionInterface::kAnswer, sdp);
  LOG_F( INFO ) << "Done";
}

void PeerControl::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> channel) {
  LOG_F( INFO ) << "remote_id_ is " << remote_id_;

  PeerDataChannelObserver* Observer = new PeerDataChannelObserver(channel);
  remote_data_channel_ = std::unique_ptr<PeerDataChannelObserver>(Observer);
  Attach(remote_data_channel_.get());

  LOG_F( INFO ) << "Done";
}

void PeerControl::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) {
  // NOTHING
}

void PeerControl::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {

  //
  // Closing sequence
  //  kIceConnectionDisconnected -> kIceConnectionClosed
  //

  switch (new_state) {
  case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionClosed:
    //
    // Ice connection has been closed.
    // Notify it to Control so the Control will remove peer in peers_
    //
    LOG_F( INFO ) << "new_state is " << "kIceConnectionClosed";
    OnPeerDisconnected();
    break;

  case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionDisconnected:
    //
    // Peer disconnected and notify it to control that makes control trigger closing
    //
    LOG_F( INFO ) << "new_state is " << "kIceConnectionDisconnected";
    OnPeerDisconnected();
    break;
  case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionNew:
    LOG_F( INFO ) << "new_state is " << "kIceConnectionNew";
    break;
  case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionChecking:
    LOG_F( INFO ) << "new_state is " << "kIceConnectionChecking";
    break;
  case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionConnected:
    LOG_F( INFO ) << "new_state is " << "kIceConnectionConnected";
    break;
  case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionCompleted:
    LOG_F( INFO ) << "new_state is " << "kIceConnectionCompleted";
    break;
  case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionFailed:
    LOG_F( INFO ) << "new_state is " << "kIceConnectionFailed";
    break;
  default:
    break;
  }
}


void PeerControl::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
  string sdp;
  if (!candidate->ToString(&sdp)) return;

  Json::Value data;

  data["sdp_mid"] = candidate->sdp_mid();
  data["sdp_mline_index"] = candidate->sdp_mline_index();
  data["candidate"] = sdp;

  control_->SendCommand(remote_id_, "ice_candidate", data);
  LOG_F( INFO ) << "Done";
}

void PeerControl::OnSuccess(webrtc::SessionDescriptionInterface* desc) {

  // This callback should take the ownership of |desc|.
  std::unique_ptr<webrtc::SessionDescriptionInterface> owned_desc(desc);
  string sdp;

  if (!desc->ToString(&sdp)) return;

  if ( state_ != pConnecting ) {
    LOG_F( WARNING ) << "Invalid state";
    return;
  }

  // Set local description
  SetLocalDescription(desc->type(), sdp);

  //
  // Send message to other peer
  Json::Value data;

  if (desc->type() == webrtc::SessionDescriptionInterface::kOffer) {
    data["sdp"] = sdp;
    control_->SendCommand(remote_id_, "offersdp", data);
  }
  else if (desc->type() == webrtc::SessionDescriptionInterface::kAnswer) {
    data["sdp"] = sdp;
    control_->SendCommand(remote_id_, "answersdp", data);
  }
  LOG_F( INFO ) << "Done";
}

void PeerControl::OnPeerOpened() {

  // Both local_data_channel_ and remote_data_channel_ has been opened
  if (local_data_channel_.get() != nullptr && remote_data_channel_.get() != nullptr &&
      local_data_channel_->state() == webrtc::DataChannelInterface::DataState::kOpen &&
      remote_data_channel_->state() == webrtc::DataChannelInterface::DataState::kOpen
    ) {
    LOG_F( INFO ) << "Peers are connected, " << remote_id_ << " and " << local_id_;
    RTC_DCHECK( state_ == pConnecting );
 
    // Fianlly, data-channel has been opened.
    state_ = pOpen;
    control_->OnPeerConnect(remote_id_);
    control_->OnPeerWritable(local_id_);
  }

  LOG_F( INFO ) << "Done";
}

void PeerControl::OnPeerDisconnected() {

  if ( state_ == pClosed ) {
    LOG_F( WARNING ) << "Already closed";
    return;
  }
  else if ( state_ == pClosing ) {
    LOG_F( INFO ) << "Already closing";
    return;
  }

  //
  // As entering here, we can make sure that the remote peer has been
  // disconnected abnormally, because previous state_ is not pClosing.
  // It will be in state pClosing if a user calls Close() manually
  //

  control_->ClosePeer( remote_id_, CLOSE_GOING_AWAY, FORCE_QUEUING_ON );
}


void PeerControl::OnPeerMessage(const webrtc::DataBuffer& buffer) {
  string data;
  control_->OnPeerMessage(remote_id_, buffer.data.data<char>(), buffer.data.size());
}

void PeerControl::OnBufferedAmountChange(const uint64_t previous_amount) {
  if ( !local_data_channel_->IsWritable() ) {
    LOG_F( LERROR ) << "local_data_channel_ is not writable";
    return;
  }
  control_->OnPeerWritable( remote_id_ );
}


bool PeerControl::CreateDataChannel(
                    const string& label,
                    const webrtc::DataChannelInit& init) {

  rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel;

  data_channel = peer_connection_->CreateDataChannel(label, &init);
  if (data_channel.get() == nullptr) {
    LOG_F( LERROR ) << "data_channel is null";
    return false;
  }

  local_data_channel_.reset(new PeerDataChannelObserver(data_channel));
  if (local_data_channel_.get() == NULL) {
    LOG_F( LERROR ) << "local_data_channel_ is null";
    return false;
  }

  Attach(local_data_channel_.get());

  LOG_F( INFO ) << "Done";
  return true;
}

void PeerControl::AddIceCandidate(const string& sdp_mid, int sdp_mline_index,
                                  const string& candidate) {

  std::unique_ptr<webrtc::IceCandidateInterface> owned_candidate(
    webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, candidate, NULL));

  peer_connection_->AddIceCandidate(owned_candidate.get());
  LOG_F( INFO ) << "Done";
}


bool PeerControl::CreatePeerConnection() {
  RTC_DCHECK(peer_connection_factory_.get() != NULL);
  RTC_DCHECK(peer_connection_.get() == NULL);

  // Enable DTLS
  webrtc::FakeConstraints constraints;
  constraints.AddOptional(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp, "true");

  // CreatePeerConnection with RTCConfiguration.
  webrtc::PeerConnectionInterface::RTCConfiguration config;
  webrtc::PeerConnectionInterface::IceServer ice_server;
  ice_server.uri = "stun:stun.l.google.com:19302";
  config.servers.push_back(ice_server);

  peer_connection_ = peer_connection_factory_->CreatePeerConnection(
    config, &constraints, NULL, NULL, this);

  if ( peer_connection_.get() == nullptr ) {
    LOG_F( LERROR ) << "peer_connection is null";
    return false;
  }

  return true;
}

void PeerControl::DeletePeerConnection() {
  Detach(remote_data_channel_.get());
  Detach(local_data_channel_.get());

  remote_data_channel_ = NULL;
  local_data_channel_ = NULL;
  peer_connection_ = NULL;
  peer_connection_factory_ = NULL;

  LOG_F( INFO ) << "Done";
}

void PeerControl::SetLocalDescription(const string& type,
                                              const string& sdp) {

  if ( peer_connection_ == nullptr ) {
    LOG_F( LERROR ) << "peer_connection_ is nullptr";
    return;
  }

  rtc::scoped_refptr<webrtc::MockSetSessionDescriptionObserver>
    observer(new rtc::RefCountedObject<
      webrtc::MockSetSessionDescriptionObserver>());
  peer_connection_->SetLocalDescription(
    observer, webrtc::CreateSessionDescription(type, sdp, NULL));

  LOG_F( INFO ) << "Done";
}

void PeerControl::SetRemoteDescription(const string& type,
                                       const string& sdp) {

  rtc::scoped_refptr<webrtc::MockSetSessionDescriptionObserver>
    observer(new rtc::RefCountedObject<
      webrtc::MockSetSessionDescriptionObserver>());
  peer_connection_->SetRemoteDescription(
    observer, webrtc::CreateSessionDescription(type, sdp, NULL));

  LOG_F( INFO ) << "Done";
}

void PeerControl::Attach(PeerDataChannelObserver* datachannel) {
  if (datachannel == nullptr) {
    LOG_F(WARNING) << "Attach to nullptr";
    return;
  }

  datachannel->SignalOnOpen_.connect(this, &PeerControl::OnPeerOpened);
  datachannel->SignalOnDisconnected_.connect(this, &PeerControl::OnPeerDisconnected);
  datachannel->SignalOnMessage_.connect(this, &PeerControl::OnPeerMessage);
  datachannel->SignalOnBufferedAmountChange_.connect(this, &PeerControl::OnBufferedAmountChange);
  LOG_F( INFO ) << "Done";
}

void PeerControl::Detach(PeerDataChannelObserver* datachannel) {
  if (datachannel == nullptr) {
    LOG_F(WARNING) << "Detach from nullptr";
    return;
  }

  datachannel->SignalOnOpen_.disconnect(this);
  datachannel->SignalOnDisconnected_.disconnect(this);
  datachannel->SignalOnMessage_.disconnect(this);
  datachannel->SignalOnBufferedAmountChange_.disconnect(this);
  LOG_F( INFO ) << "Done";
}



//
// class PeerDataChannelObserver
//

PeerDataChannelObserver::PeerDataChannelObserver(webrtc::DataChannelInterface* channel)
  : channel_(channel) {
  channel_->RegisterObserver(this);
  state_ = channel_->state();
  LOG_F( INFO ) << "Done";
}

PeerDataChannelObserver::~PeerDataChannelObserver() {
  channel_->Close();
  state_ = channel_->state();
  channel_->UnregisterObserver();
  LOG_F( INFO ) << "Done";
}

void PeerDataChannelObserver::OnBufferedAmountChange(uint64_t previous_amount) {
  SignalOnBufferedAmountChange_(previous_amount);

  if (channel_->buffered_amount() == 0) {
    std::lock_guard<std::mutex> lk(send_lock_);
    send_cv_.notify_all();
  }

  return;
}

void PeerDataChannelObserver::OnStateChange() {
  state_ = channel_->state();
  if (state_ == webrtc::DataChannelInterface::DataState::kOpen) {
    LOG_F( INFO ) << "Data channel internal state is kOpen";
    SignalOnOpen_();
  }
  else if (state_ == webrtc::DataChannelInterface::DataState::kClosed) {
    LOG_F( INFO ) << "Data channel internal state is kClosed";
    SignalOnDisconnected_();
  }
}

void PeerDataChannelObserver::OnMessage(const webrtc::DataBuffer& buffer) {
  SignalOnMessage_(buffer);
}

bool PeerDataChannelObserver::Send(const char* buffer, const size_t size) {
  rtc::CopyOnWriteBuffer rtcbuffer(buffer, size);
  webrtc::DataBuffer databuffer(rtcbuffer, true);

  if ( channel_->buffered_amount() >= max_buffer_size_ ) {
    LOG_F( LERROR ) << "Buffer is full";
    return false;
  }

  return channel_->Send(databuffer);
}

bool PeerDataChannelObserver::SyncSend(const char* buffer, const size_t size) {
  rtc::CopyOnWriteBuffer rtcbuffer(buffer, size);
  webrtc::DataBuffer databuffer(rtcbuffer, true);

  std::unique_lock<std::mutex> lock(send_lock_);
  if (!channel_->Send(databuffer)) return false;

  if (!send_cv_.wait_for(lock, std::chrono::milliseconds(60*1000),
                         [this] () { return channel_->buffered_amount() == 0; })) {
    LOG_F( LERROR ) << "Buffer is full";
    return false;
  }

  return true;
}

void PeerDataChannelObserver::Close() {
  LOG_F(LS_INFO) << "Closing data channel";
  if (channel_->state() != webrtc::DataChannelInterface::kClosing) {
    channel_->Close();
  }
}


bool PeerDataChannelObserver::IsOpen() const {
  return state_ == webrtc::DataChannelInterface::kOpen;
}

uint64_t PeerDataChannelObserver::BufferedAmount() {
  return channel_->buffered_amount();
}

bool PeerDataChannelObserver::IsWritable() {
  if ( channel_ == nullptr ) {
    LOG_F( LERROR ) << "channel_ is null";
    return false;
  }

  if ( !IsOpen() ) {
    LOG_F( LERROR ) << "data channel is not opened";
    return false;
  }

  if ( channel_->buffered_amount() > 0 ) {
    LOG_F( LERROR ) << "buffer is full";
    return false;
  }

  return true;
}


const webrtc::DataChannelInterface::DataState
PeerDataChannelObserver::state() const {
  return channel_->state();
}


} // namespace peerapi