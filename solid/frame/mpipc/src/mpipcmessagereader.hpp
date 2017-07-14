// solid/frame/ipc/src/ipcmessagereader.hpp
//
// Copyright (c) 2015 Valentin Palade (vipalade @ gmail . com)
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//

#pragma once

#include "solid/frame/mpipc/mpipcprotocol.hpp"
#include "solid/system/common.hpp"
#include "solid/system/error.hpp"
#include <deque>

namespace solid {
namespace frame {
namespace mpipc {

struct ReaderConfiguration;

struct PacketHeader;

class MessageReader {
public:
    struct Receiver {
        uint8_t request_buffer_ack_count_;
        
        Receiver():request_buffer_ack_count_(0){}
        
        virtual ~Receiver() {}

        virtual void receiveMessage(MessagePointerT&, const size_t /*_msg_type_id*/) = 0;
        virtual void receiveKeepAlive()                     = 0;
        virtual void receiveAckCount(uint8_t _count)        = 0;
        virtual void receiveCancelRequest(const RequestId&) = 0;
        virtual bool receiveRelayBody(MessageHeader& _rmsghdr, const char* _pbeg, size_t _sz, ObjectIdT& _rrelay_id, ErrorConditionT& _rerror)
        {
            return false;
        }
        virtual void pushCancelRequest(const RequestId&){}
    };

    MessageReader();

    ~MessageReader();

    uint32_t read(
        const char*                _pbuf,
        uint32_t                   _bufsz,
        Receiver&                  _receiver,
        ReaderConfiguration const& _rconfig,
        Protocol const&            _rproto,
        ConnectionContext&         _rctx,
        ErrorConditionT&           _rerror);

    void prepare(ReaderConfiguration const& _rconfig);
    void unprepare();

private:
    void doConsumePacket(
        const char*                _pbuf,
        PacketHeader const&        _packet_header,
        Receiver&                  _receiver,
        ReaderConfiguration const& _rconfig,
        Protocol const&            _rproto,
        ConnectionContext&         _rctx,
        ErrorConditionT&           _rerror);

    const char* doConsumeMessage(
        const char*        _pbufpos,
        const char* const  _pbufend,
        const uint32_t     _msgidx,
        const uint8_t      _msg_type,
        Receiver&          _receiver,
        Protocol const&    _rproto,
        ConnectionContext& _rctx,
        ErrorConditionT&   _rerror);

private:
    enum struct StateE {
        ReadPacketHead = 1,
        ReadPacketBody,
    };

    struct MessageStub {
        enum struct StateE : uint8_t {
            NotStarted,
            ReadHead,
            ReadBody,
            RelayBody,
            RelayFail,
        };

        MessagePointerT      message_ptr_;
        DeserializerPointerT deserializer_ptr_;
        MessageHeader        message_header_;
        size_t               packet_count_;
        ObjectIdT            relay_id;
        StateE               state_;

        MessageStub()
            : packet_count_(0)
            , state_(StateE::NotStarted)
        {
        }

        void clear()
        {
            message_ptr_.reset();
            deserializer_ptr_.reset();
            packet_count_ = 0;
            state_        = StateE::NotStarted;
        }
    };
    using MessageVectorT = std::deque<MessageStub>;

    MessageVectorT message_vec_;
    uint64_t       current_message_type_id_;
    StateE         state_;
};

} //namespace mpipc
} //namespace frame
} //namespace solid
