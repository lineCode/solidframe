// frame/ipc/src/ipcmessagewriter.hpp
//
// Copyright (c) 2015 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#ifndef SOLID_FRAME_IPC_SRC_IPC_MESSAGE_WRITER_HPP
#define SOLID_FRAME_IPC_SRC_IPC_MESSAGE_WRITER_HPP

#include "system/common.hpp"
#include "system/error.hpp"
#include "system/specific.hpp"

#include "utility/queue.hpp"
#include "utility/stack.hpp"

#include "ipcutility.hpp"
#include "frame/ipc/ipcserialization.hpp"




namespace solid{
namespace frame{
namespace ipc{

struct Configuration;

struct Serializer: public SerializerT, public SpecificObject{
};

typedef std::unique_ptr<Serializer>		SerializerPointerT;

class MessageWriter{
public:
	MessageWriter();
	~MessageWriter();
	
	void enqueue(
		MessagePointerT &_rmsgptr,
		const size_t _msg_type_idx,
		ulong _flags,
		Configuration const &_rconfig,
		TypeIdMapT const &_ridmap,
		ConnectionContext &_rctx
	);
	
	uint32 write(
		char *_pbuf,
		uint32 _bufsz, const bool _keep_alive, 
		Configuration const &_rconfig,
		TypeIdMapT const &_ridmap,
		ConnectionContext &_rctx,
		ErrorConditionT &_rerror
	);
	void completeMessage(
		MessageUid const &_rmsguid,
		TypeIdMapT const &_ridmap,
		ConnectionContext &_rctx
	);
	
	void prepare(Configuration const &_rconfig);
	void unprepare();
private:
	struct PendingMessageStub{
		PendingMessageStub(
			MessagePointerT &_rmsgptr,
			const size_t _msg_type_idx,
			ulong _flags
		): msgptr(std::move(_rmsgptr)), msg_type_idx(_msg_type_idx), flags(_flags){}
		
		MessagePointerT msgptr;
		const size_t	msg_type_idx;
		ulong			flags;
	};
	
	struct MessageStub{
		MessageStub(
			MessagePointerT &_rmsgptr,
			const size_t _msg_type_idx,
			ulong _flags
		): msgptr(std::move(_rmsgptr)), msg_type_idx(_msg_type_idx), flags(_flags){}
		
		MessageStub():msg_type_idx(-1), flags(-1), unique(0){}
		
		MessagePointerT msgptr;
		size_t			msg_type_idx;
		ulong			flags;
		uint32			unique;
	};
	typedef Queue<PendingMessageStub>							PendingMessageQueueT;
	typedef std::vector<MessageStub>							MessageVectorT;
	struct WriteStub{
		WriteStub(
			size_t _idx = -1,
			size_t _packet_count = 0, 
			Serializer *_pser = nullptr
		):idx(_idx), packet_count(_packet_count), serializer_ptr(_pser){}
		
		WriteStub(
			WriteStub &&_ws
		):idx(_ws.idx), packet_count(_ws.packet_count), serializer_ptr(std::move(_ws.serializer_ptr)){}
		
		size_t				idx;
		size_t				packet_count;
		SerializerPointerT	serializer_ptr;
	};
	
	
	struct PacketOptions{
		PacketOptions(): packet_type(PacketHeader::NewMessageTypeE), force_no_compress(false){}
		
		PacketHeader::Types		packet_type;
		bool 					force_no_compress;
	};
	
	typedef Queue<WriteStub>									WriteQueueT;
	typedef Stack<size_t>										CacheStackT;
	
	char* doFillPacket(
		char* _pbufbeg,
		char* _pbufend,
		PacketOptions &_rpacket_options,
		bool &_rmore,
		ipc::Configuration const &_rconfig,
		TypeIdMapT const & _ridmap,
		ConnectionContext &_rctx,
		ErrorConditionT & _rerror
	);
	
private:
	PendingMessageQueueT		pending_message_q;
	MessageVectorT				message_vec;
	WriteQueueT					write_q;
	CacheStackT					cache_stk;
};


}//namespace ipc
}//namespace frame
}//namespace solid


#endif