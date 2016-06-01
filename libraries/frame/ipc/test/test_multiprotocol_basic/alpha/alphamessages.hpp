#ifndef TEST_MULTIPROTOCOL_ALPHA_PROTOCOL_HPP
#define TEST_MULTIPROTOCOL_ALPHA_PROTOCOL_HPP

#include "system/common.hpp"
#include "frame/ipc/ipcmessage.hpp"
#include "frame/ipc/ipccontext.hpp"




namespace alpha_protocol{

struct FirstMessage: solid::frame::ipc::Message{
	uint32_t			v;
	std::string			str;

	template <class S>
	void serialize(S &_s, solid::frame::ipc::ConnectionContext &_rctx){
		_s.push(str, "str");
		_s.push(v, "v");
	}	
};

struct SecondMessage: solid::frame::ipc::Message{
	uint64_t			v;
	std::string			str;

	template <class S>
	void serialize(S &_s, solid::frame::ipc::ConnectionContext &_rctx){
		_s.push(str, "str");
		_s.push(v, "v");
	}	
};


struct ThirdMessage: solid::frame::ipc::Message{
	uint16_t			v;
	std::string			str;

	template <class S>
	void serialize(S &_s, solid::frame::ipc::ConnectionContext &_rctx){
		_s.push(str, "str");
		_s.push(v, "v");
	}	
};

}//namespace

#endif