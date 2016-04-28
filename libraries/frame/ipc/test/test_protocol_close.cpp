#include "test_protocol_common.hpp"
#include "system/exception.hpp"
#include "frame/ipc/ipcerror.hpp"

using namespace solid;


namespace{

struct InitStub{
	size_t		size;
	ulong		flags;
};

InitStub initarray[] = {
	{100000, 0},
	{16384000, 0},
	{8192000, 0},
	{4096000, 0},
	{2048000, 0},
	{1024000, 0},
	{512000, frame::ipc::Message::SynchronousFlagE},
	{256000, 0},
	{128000, 0},
	{64000, 0},
	{32000, 0},
	{16000, 0},
	{8000, 0},
	{4000, 0},
	{2000, 0
		
	},
};

std::string						pattern;
const size_t					initarraysize = sizeof(initarray)/sizeof(InitStub);

size_t							crtwriteidx = 0;
size_t							crtreadidx  = 0;
size_t							writecount = 0;


size_t real_size(size_t _sz){
	//offset + (align - (offset mod align)) mod align
	return _sz + ((sizeof(uint64) - (_sz % sizeof(uint64))) % sizeof(uint64));
}

struct Message: Dynamic<Message, frame::ipc::Message>{
	uint32							idx;
    std::string						str;
	
	Message(uint32 _idx):idx(_idx){
		idbg("CREATE ---------------- "<<(void*)this<<" idx = "<<idx);
		init();
		
	}
	Message(){
		idbg("CREATE ---------------- "<<(void*)this);
	}
	~Message(){
		idbg("DELETE ---------------- "<<(void*)this);
	}

	template <class S>
	void serialize(S &_s, frame::ipc::ConnectionContext &_rctx){
		_s.push(str, "str");
		_s.push(idx, "idx");
		
	}
	
	void init(){
		const size_t	sz = real_size(initarray[idx % initarraysize].size);
		str.resize(sz);
		const size_t	count = sz / sizeof(uint64);
		uint64			*pu  = reinterpret_cast<uint64*>(const_cast<char*>(str.data()));
		const uint64	*pup = reinterpret_cast<const uint64*>(pattern.data());
		const size_t	pattern_size = pattern.size() / sizeof(uint64);
		for(uint64 i = 0; i < count; ++i){
			pu[i] = pup[i % pattern_size];//pattern[i % pattern.size()];
		}
	}
	bool check()const{
		const size_t	sz = real_size(initarray[idx % initarraysize].size);
		idbg("str.size = "<<str.size()<<" should be equal to "<<sz);
		if(sz != str.size()){
			return false;
		}
		const size_t	count = sz / sizeof(uint64);
		const uint64	*pu = reinterpret_cast<const uint64*>(str.data());
		const uint64	*pup = reinterpret_cast<const uint64*>(pattern.data());
		const size_t	pattern_size = pattern.size() / sizeof(uint64);
		
		for(uint64 i = 0; i < count; ++i){
			if(pu[i] != pup[i % pattern_size]) return false;
		}
		return true;
	}
	
};

void complete_message(
	frame::ipc::ConnectionContext &_rctx,
	frame::ipc::MessagePointerT &_rmessage_ptr,
	frame::ipc::MessagePointerT &_rresponse_ptr,
	ErrorConditionT const &_rerr
);

struct Context{
	Context():ipcreaderconfig(nullptr), ipcwriterconfig(nullptr), ipctypemap(nullptr), ipcmsgreader(nullptr), ipcmsgwriter(nullptr){}
	
	frame::ipc::ReaderConfiguration	*ipcreaderconfig;
	frame::ipc::WriterConfiguration	*ipcwriterconfig;
	frame::ipc::TypeIdMapT			*ipctypemap;
	frame::ipc::MessageReader		*ipcmsgreader;
	frame::ipc::MessageWriter		*ipcmsgwriter;

	
}								ctx;
frame::ipc::ConnectionContext	&ipcconctx(frame::ipc::TestEntryway::createContext());



void complete_message(
	frame::ipc::ConnectionContext &_rctx,
	frame::ipc::MessagePointerT &_rmessage_ptr,
	frame::ipc::MessagePointerT &_rresponse_ptr,
	ErrorConditionT const &_rerr
){
	if(_rmessage_ptr.get()){
		idbg(static_cast<Message*>(_rmessage_ptr.get())->idx);
	}
	
	if(_rresponse_ptr.get()){
	
		if(not static_cast<Message&>(*_rresponse_ptr).check()){
			THROW_EXCEPTION("Message check failed.");
		}
		
		++crtreadidx;
		idbg(crtreadidx);
		
		if(crtwriteidx < writecount){
			frame::ipc::MessageBundle	msgbundle;
			frame::ipc::MessageId		writer_msg_id;
			frame::ipc::MessageId		pool_msg_id;
			
			msgbundle.message_flags = initarray[crtwriteidx % initarraysize].flags;
			msgbundle.message_ptr = std::move(frame::ipc::MessagePointerT(new Message(crtwriteidx)));
			//msgbundle.complete_fnc = std::move(response_fnc);
			msgbundle.message_type_id = ctx.ipctypemap->index(msgbundle.message_ptr.get());
			
			bool rv = ctx.ipcmsgwriter->enqueue(
				*ctx.ipcwriterconfig, msgbundle, pool_msg_id, writer_msg_id
			);
			
			idbg("enqueue rv = "<<rv<<" writer_msg_id = "<<writer_msg_id);
			idbg(frame::ipc::MessageWriterPrintPairT(*ctx.ipcmsgwriter, frame::ipc::MessageWriter::PrintInnerListsE));
			++crtwriteidx;
		}
	}
}

}//namespace
 
int test_protocol_close(int argc, char **argv){
	
	Thread::init();
#ifdef SOLID_HAS_DEBUG
	Debug::the().levelMask("ew");
	Debug::the().moduleMask("all");
	Debug::the().initStdErr(false, nullptr);
#endif
	
	for(int i = 0; i < 127; ++i){
		if(isprint(i) and !isblank(i)){
			pattern += static_cast<char>(i);
		}
	}
	
	size_t	sz = real_size(pattern.size());
	
	if(sz > pattern.size()){
		pattern.resize(sz - sizeof(uint64));
	}else if(sz < pattern.size()){
		pattern.resize(sz);
	}
	
	const uint16					bufcp(1024*4);
	char							buf[bufcp];
	
	frame::ipc::WriterConfiguration	ipcwriterconfig;
	frame::ipc::ReaderConfiguration	ipcreaderconfig;
	frame::ipc::TypeIdMapT			ipctypemap;
	frame::ipc::MessageReader		ipcmsgreader;
	frame::ipc::MessageWriter		ipcmsgwriter;
	
	ErrorConditionT					error;
	
	
	ctx.ipcreaderconfig		= &ipcreaderconfig;
	ctx.ipcwriterconfig		= &ipcwriterconfig;
	ctx.ipctypemap			= &ipctypemap;
	ctx.ipcmsgreader		= &ipcmsgreader;
	ctx.ipcmsgwriter		= &ipcmsgwriter;
	
	frame::ipc::TestEntryway::initTypeMap<::Message>(ipctypemap, complete_message);
	
	const size_t					start_count = 10;
	
	writecount = 16;//start_count;//
	
	for(; crtwriteidx < start_count; ++crtwriteidx){
		frame::ipc::MessageBundle	msgbundle;
		frame::ipc::MessageId		writer_msg_id;
		frame::ipc::MessageId		pool_msg_id;
		
		msgbundle.message_flags = initarray[crtwriteidx % initarraysize].flags;
		msgbundle.message_ptr = std::move(frame::ipc::MessagePointerT(new Message(crtwriteidx)));
		msgbundle.message_type_id = ctx.ipctypemap->index(msgbundle.message_ptr.get());
		
		
		bool rv = ipcmsgwriter.enqueue(
			ipcwriterconfig, msgbundle, pool_msg_id, writer_msg_id
		);
		
		cassert(rv);
		
		idbg("enqueue rv = "<<rv<<" writer_msg_id = "<<writer_msg_id);
		idbg(frame::ipc::MessageWriterPrintPairT(ipcmsgwriter, frame::ipc::MessageWriter::PrintInnerListsE));
	}
	if(1){
		frame::ipc::MessageBundle	msgbundle;
		frame::ipc::MessageId		writer_msg_id;
		frame::ipc::MessageId		pool_msg_id;
		
		bool rv = ipcmsgwriter.enqueue(
			ipcwriterconfig, msgbundle, pool_msg_id, writer_msg_id
		);
		idbg("enqueue close rv = "<<rv<<" writer_msg_id = "<<writer_msg_id);
	}
	
	
	{
		auto	reader_complete_lambda(
			[&ipctypemap](const frame::ipc::MessageReader::Events _event, frame::ipc::MessagePointerT & _rresponse_ptr, const size_t _message_type_id){
				switch(_event){
					case frame::ipc::MessageReader::MessageCompleteE:{
						idbg("reader complete message");
						frame::ipc::MessagePointerT		message_ptr;
						ErrorConditionT					error;
						ipctypemap[_message_type_id].complete_fnc(ipcconctx, message_ptr, _rresponse_ptr, error);
					}break;
					case frame::ipc::MessageReader::KeepaliveCompleteE:
						idbg("complete keepalive");
						break;
				}
			}
		);
		
		auto	writer_complete_lambda(
			[&ipctypemap](frame::ipc::MessageBundle &_rmsgbundle, frame::ipc::MessageId const &_rmsgid){
				idbg("writer complete message");
				frame::ipc::MessagePointerT		response_ptr;
				ErrorConditionT					error;
				ipctypemap[_rmsgbundle.message_type_id].complete_fnc(ipcconctx, _rmsgbundle.message_ptr, response_ptr, error);
				return ErrorConditionT();
			}
		);
		
		frame::ipc::MessageReader::CompleteFunctionT	readercompletefnc(std::cref(reader_complete_lambda));
		frame::ipc::MessageWriter::CompleteFunctionT	writercompletefnc(std::cref(writer_complete_lambda));
		
		ipcmsgreader.prepare(ipcreaderconfig);
		
		bool is_running = true;
		
		while(is_running and !error){
			uint32 bufsz = ipcmsgwriter.write(buf, bufcp, false, writercompletefnc, ipcwriterconfig, ipctypemap, ipcconctx, error);
			
			if(!error and bufsz){
				
				ipcmsgreader.read(buf, bufsz, readercompletefnc, ipcreaderconfig, ipctypemap, ipcconctx, error);
			}else{
				idbg("done write");
				cassert(error == frame::ipc::error_connection_delayed_closed);
				is_running = false;
			}
		}
	}
	
	return 0;
}

