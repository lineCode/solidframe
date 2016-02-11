// frame/ipc/src/ipcservice.cpp
//
// Copyright (c) 2015 Valentin Palade (vipalade @ gmail . com) 
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#include <vector>
#include <cstring>
#include <utility>
#include <algorithm>

#include "system/debug.hpp"
#include "system/mutex.hpp"
#include "system/socketdevice.hpp"
#include "system/specific.hpp"
#include "system/exception.hpp"

#include "utility/queue.hpp"
#include "utility/binaryseeker.hpp"

#include "frame/common.hpp"
#include "frame/manager.hpp"

#include "frame/aio/aioresolver.hpp"

#include "frame/ipc/ipcservice.hpp"
#include "frame/ipc/ipccontext.hpp"
#include "frame/ipc/ipcmessage.hpp"
#include "frame/ipc/ipcconfiguration.hpp"
#include "frame/ipc/ipcerror.hpp"

#include "system/mutualstore.hpp"
#include "system/atomic.hpp"
#include "utility/queue.hpp"
#include "utility/stack.hpp"
#include "utility/string.hpp"

#include "ipcutility.hpp"
#include "ipcconnection.hpp"
#include "ipclistener.hpp"


#include <vector>
#include <deque>

#ifdef SOLID_USE_CPP11
#define CPP11_NS std
#include <unordered_map>
#else
#include "boost/unordered_map.hpp"
#define CPP11_NS boost
#endif

namespace solid{
namespace frame{
namespace ipc{
//=============================================================================
typedef CPP11_NS::unordered_map<
	const char*, size_t, CStringHash, CStringEqual
>														NameMapT;
typedef Queue<ObjectIdT>								ObjectIdQueueT;

enum{
	InnerLinkOrder = 0,
	InnerLinkAsync,
	InnerLinkCount
};

//-----------------------------------------------------------------------------

struct MessageStub: InnerNode<InnerLinkCount>{
	
	enum{
		CancelableFlag = 1
	};
	
	MessageStub(
		MessagePointerT &_rmsgptr,
		const size_t _msg_type_idx,
		ResponseHandlerFunctionT &_rresponse_fnc,
		ulong _msgflags
	):	msgbundle(_rmsgptr, _msg_type_idx, _msgflags, _rresponse_fnc),
		unique(0), flags(0){}
	
	MessageStub(
		MessageStub && _rmsg
	):	InnerNode<InnerLinkCount>(std::move(_rmsg)),
		msgbundle(std::move(_rmsg.msgbundle)),
		msgid(_rmsg.msgid), objid(_rmsg.objid),
		unique(_rmsg.unique), flags(_rmsg.flags){}
	
	MessageStub():unique(0), flags(0){}
	
	bool isCancelable()const{
		return (flags & CancelableFlag) != 0;
	}
	
	void makeCancelable(){
		flags |= CancelableFlag;
	}
	
	void clear(){
		msgbundle.clear();
		msgid = MessageId();
		objid = ObjectIdT();
		++unique;
		flags = 0;
	}
	
	MessageBundle	msgbundle;
	MessageId		msgid;
	ObjectIdT		objid;
	uint32			unique;
	uint			flags;
};

//-----------------------------------------------------------------------------

using MessageVectorT = std::vector<MessageStub>;
using MessageOrderInnerListT = InnerList<MessageVectorT, InnerLinkOrder>;
using MessageCacheInnerListT = InnerList<MessageVectorT, InnerLinkOrder>;
using MessageAsyncInnerListT = InnerList<MessageVectorT, InnerLinkAsync>;

//-----------------------------------------------------------------------------

std::ostream& operator<<(std::ostream &_ros, const MessageOrderInnerListT &_rlst){
	size_t cnt = 0;
	_rlst.forEach(
		[&_ros, &cnt](size_t _idx, const MessageStub& _rmsg){
			_ros<<_idx<<' ';
			++cnt;
		}
	);
	cassert(cnt == _rlst.size());
	return _ros;
}

//-----------------------------------------------------------------------------

struct ConnectionPoolStub{
	uint32					unique;
	size_t					pending_connection_count;
	size_t					active_connection_count;
	size_t					pending_resolve_count;
	std::string				name;
	ObjectIdT 				synchronous_connection_id;
	MessageVectorT			msgvec;
	MessageOrderInnerListT	msgorder_inner_list;
	MessageCacheInnerListT	msgcache_inner_list;
	MessageAsyncInnerListT	msgasync_inner_list;
	
	ObjectIdQueueT			conn_waitingq;
	
	ObjectIdT				msg_cancel_connection_id;
	bool					msg_cancel_connection_stopping;
	bool					is_stopping;
	
	ConnectionPoolStub(
	):	unique(0), pending_connection_count(0), active_connection_count(0),
		pending_resolve_count(0),
		msgorder_inner_list(msgvec), msgcache_inner_list(msgvec), msgasync_inner_list(msgvec),
		msg_cancel_connection_stopping(false), is_stopping(false){}
	
	ConnectionPoolStub(
		ConnectionPoolStub && _rconpool
	):	unique(_rconpool.unique), pending_connection_count(_rconpool.pending_connection_count),
		active_connection_count(_rconpool.active_connection_count),
		pending_resolve_count(_rconpool.pending_resolve_count),
		name(std::move(_rconpool.name)),
		synchronous_connection_uid(_rconpool.synchronous_connection_uid),
		msgvec(std::move(_rconpool.msgvec)),
		msgorder_inner_list(msgvec, _rconpool.msgorder_inner_list),
		msgcache_inner_list(msgvec, _rconpool.msgcache_inner_list),
		conn_waitingq(std::move(_rconpool.conn_waitingq)),
		msg_cancel_connection_id(_rconpool.msg_cancel_connection_id),
		msg_cancel_connection_stopping(_rconpool.msg_cancel_connection_stopping),
		is_stopping(_rconpool.is_stopping){}
	
	void clear(){
		name.clear();
		synchronous_connection_uid = ObjectIdT();
		++unique;
		pending_connection_count = 0;
		active_connection_count = 0;
		pending_resolve_count = 0;
		while(conn_waitingq.size()){
			conn_waitingq.pop();
		}
		cassert(msgorder_inner_list.empty());
		cassert(msgasync_inner_list.empty());
		msgcache_inner_list.clear();
		msgorder_inner_list.clear();
		msgasync_inner_list.clear();
		msgvec.clear();
		msgvec.shrink_to_fit();
		msg_cancel_connection_stopping = false;
		is_stopping = false;
		cassert(msgorder_inner_list.check());
	}
	
	MessageId insertMessage(
		MessagePointerT &_rmsgptr,
		const size_t _msg_type_idx,
		ResponseHandlerFunctionT &_rresponse_fnc,
		ulong _flags
	){
		size_t		idx;
		
		if(msgcache_inner_list.size()){
			idx = msgcache_inner_list.frontIndex();
			msgcache_inner_list.popFront();
		}else{
			idx = msgvec.size();
			msgvec.push_back(MessageStub{});
		}
		
		MessageStub	&rmsgstub{msgvec[idx]};
		
		rmsgstub.msgbundle = MessageBundle(_rmsgptr, _msg_type_idx, _flags, _rresponse_fnc);
		return MessageId(idx, rmsgstub.unique);
	}
	
	MessageId pushBackMessage(
		MessagePointerT &_rmsgptr,
		const size_t _msg_type_idx,
		ResponseHandlerFunctionT &_rresponse_fnc,
		ulong _flags
	){
		const MessageId msgid = insertMessage(_rmsgptr, _msg_type_idx, _rresponse_fnc, _flags);
		msgorder_inner_list.pushBack(msgid);
		idbgx(Debug::ipc, "msgorder_inner_list "<<msgorder_inner_list);
		if(Message::is_asynchronous(_flags)){
			msgasync_inner_list.pushBack(msgid);
			idbgx(Debug::ipc, "msgasync_inner_list "<<msgasync_inner_list);
		}
		cassert(msgorder_inner_list.check());
		return msgid;
	}
	
	void clearAndCacheMessage(const size_t _msg_idx){
		MessageStub	&rmsgstub{msgvec[_msg_idx]};
		rmsgstub.clear();
		msgcache_inner_list.pushBack(_msg_idx);
	}
	
	void cacheFrontMessage(){
		idbgx(Debug::ipc, "msgorder_inner_list "<<msgorder_inner_list);
		msgcache_inner_list.pushBack(msgorder_inner_list.popFront());
		
		cassert(msgorder_inner_list.check());
	}
	
	void popFrontMessage(){
		idbgx(Debug::ipc, "msgorder_inner_list "<<msgorder_inner_list);
		msgorder_inner_list.popFront();
		
		cassert(msgorder_inner_list.check());
	}
	
	void eraseMessage(const size_t _msg_idx){
		idbgx(Debug::ipc, "msgorder_inner_list "<<msgorder_inner_list);
		msgorder_inner_list.erase(_msg_idx);
		cassert(msgorder_inner_list.check());
	}
	void clearPopAndCacheMessage(const size_t _msg_idx){
		idbgx(Debug::ipc, "msgorder_inner_list "<<msgorder_inner_list);
		MessageStub	&rmsgstub{msgvec[_msg_idx]};
		
		msgorder_inner_list.erase(_msg_idx);
		
		if(Message::is_asynchronous(rmsgstub.msgbundle.message_flags)){
			msgasync_inner_list.erase(_msg_idx);
		}
		
		msgcache_inner_list.pushBack(_msg_idx);
		
		rmsgstub.clear();
		cassert(msgorder_inner_list.check());
	}
	
	void cacheMessageId(const size_t _msg_idx, const size_t _msg_uid){
		cassert(_msg_idx < msgvec.size());
		cassert(msgvec[_msg_idx].unique == _msg_uid);
		if(_msg_idx < msgvec.size() and msgvec[_msg_idx].unique == _msg_uid){
			msgvec[_msg_idx].clear();
			msgcache_inner_list.pushBack(_msg_idx);
		}
	}
	
	bool isMessageCancelConnectionStopping()const{
		return msg_cancel_connection_stopping;
	}
	
	void setMessageCancelConnectionStopping(){
		msg_cancel_connection_stopping = true;
	}
	void resetMessageCancelConnectionStopping(){
		msg_cancel_connection_stopping = false;
	}
	
	bool isStopping()const{
		return is_stopping;
	}
	void setStopping(){
		is_stopping = true;
	}
};

//-----------------------------------------------------------------------------

typedef std::deque<ConnectionPoolStub>					ConnectionPoolDequeT;
typedef Stack<size_t>									SizeStackT;

//-----------------------------------------------------------------------------

struct Service::Data{
	Data(Service &_rsvc): pmtxarr(nullptr), mtxsarrcp(0), config(ServiceProxy(_rsvc)){}
	
	~Data(){
		delete []pmtxarr;
	}
	
	
	Mutex & connectionPoolMutex(size_t _idx)const{
		return pmtxarr[_idx % mtxsarrcp];
	}
	
	void lockAllConnectionPoolMutexes(){
		for(size_t i = 0; i < mtxsarrcp; ++i){
			pmtxarr[i].lock();
		}
	}
	
	void unlockAllConnectionPoolMutexes(){
		for(size_t i = 0; i < mtxsarrcp; ++i){
			pmtxarr[i].unlock();
		}
	}
	
	Mutex					mtx;
	Mutex					*pmtxarr;
	size_t					mtxsarrcp;
	NameMapT				namemap;
	ConnectionPoolDequeT	conpooldq;
	SizeStackT				conpoolcachestk;
	Configuration			config;
};
//=============================================================================

Service::Service(
	frame::Manager &_rm
):BaseT(_rm), d(*(new Data(*this))){}
	
//! Destructor
Service::~Service(){
	delete &d;
}
//-----------------------------------------------------------------------------
Configuration const & Service::configuration()const{
	return d.config;
}
//-----------------------------------------------------------------------------
ErrorConditionT Service::reconfigure(Configuration const& _rcfg){
	this->stop(true);
	this->start();
	
	ErrorConditionT err;
	
	ServiceProxy	sp(*this);
	
	err = _rcfg.check();
	
	if(err) return err;
	
	_rcfg.message_register_fnc(sp);
	
	d.config.reset(_rcfg);
	
	if(d.config.listen_address_str.size()){
		std::string		tmp;
		const char 		*hst_name;
		const char		*svc_name;
		
		size_t off = d.config.listen_address_str.rfind(':');
		if(off != std::string::npos){
			tmp = d.config.listen_address_str.substr(0, off);
			hst_name = tmp.c_str();
			svc_name = d.config.listen_address_str.c_str() + off + 1;
			if(!svc_name[0]){
				svc_name = d.config.default_listen_port_str.c_str();
			}
		}else{
			hst_name = d.config.listen_address_str.c_str();
			svc_name = d.config.default_listen_port_str.c_str();
		}
		
		ResolveData		rd = synchronous_resolve(hst_name, svc_name, 0, -1, SocketInfo::Stream);
		SocketDevice	sd;
			
		sd.create(rd.begin());
		sd.prepareAccept(rd.begin(), 2000);
			
		if(sd.ok()){
			DynamicPointer<aio::Object>		objptr(new Listener(sd));
			
			ObjectIdT						conuid = d.config.scheduler().startObject(objptr, *this, generic_event_category.event(GenericEvents::Start), err);
			
			if(err){
				return err;
			}
		}else{
			err.assign(-1, err.category());
			return err;
		}
	}
	
	if(d.config.session_mutex_count > d.mtxsarrcp){
		delete []d.pmtxarr;
		d.pmtxarr = new Mutex[d.config.session_mutex_count];
		d.mtxsarrcp = d.config.session_mutex_count;
	}
	
	return ErrorConditionT();
}
//-----------------------------------------------------------------------------
size_t Service::doPushNewConnectionPool(){
	d.lockAllConnectionPoolMutexes();
	for(size_t i = 0; i < d.mtxsarrcp; ++i){
		size_t idx = d.conpooldq.size();
		d.conpooldq.push_back(ConnectionPoolStub());
		d.conpoolcachestk.push(d.mtxsarrcp - idx - 1);
	}
	d.unlockAllConnectionPoolMutexes();
	size_t	idx = d.conpoolcachestk.top();
	d.conpoolcachestk.pop();
	return idx;
}

//-----------------------------------------------------------------------------

struct OnRelsolveF{
	Manager		&rm;
	ObjectIdT	objuid;
	Event 		event;
	
	OnRelsolveF(
		Manager &_rm,
		const ObjectIdT &_robjuid,
		const Event &_revent
	):rm(_rm), objuid(_robjuid), event(_revent){}
	
	void operator()(AddressVectorT &_raddrvec){
		idbgx(Debug::ipc, "OnResolveF(addrvec of size "<<_raddrvec.size()<<")");
		event.any().reset(ResolveMessage(_raddrvec));
		rm.notify(objuid, std::move(event));
	}
};

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//NOTE:
//The last connection before dying MUST:
//	1. Lock service.d.mtx
//	2. Lock SessionStub.mtx
//	3. Fetch all remaining messages in the SessionStub.msgq
//	4. Destroy the SessionStub and unregister it from namemap
//	5. call complete function for every fetched message
//	6. Die

ErrorConditionT Service::doSendMessage(
	const char *_recipient_name,
	const RecipientId	&_rrecipient_id_in,
	MessagePointerT &_rmsgptr,
	ResponseHandlerFunctionT &_rresponse_fnc,
	RecipientId *_precipient_id_out,
	MessageId *_pmsguid_out,
	ulong _flags
){
	solid::ErrorConditionT		error;
	size_t						pool_idx;
	uint32						unique;
	bool						check_uid = false;
	const size_t				msg_type_idx = tm.index(_rmsgptr.get());
	
	
	if(msg_type_idx == 0){
		edbgx(Debug::ipc, this<<" message type not registered");
		error.assign(-1, error.category());//TODO:type not registered
		return error;
	}
	
	Locker<Mutex>				lock(d.mtx);
	
	if(_rrecipient_id_in.isValidConnection()){
		cassert(_precipient_id_out == nullptr);
		//directly send the message to a connection object
		return doNotifyConnectionNewMessage(
			_rrecipient_id_in,
			_rmsgptr,
			msg_type_idx,
			_rresponse_fnc,
			_pmsguid_out,
			_flags
		);
	}else if(_recipient_name){
		NameMapT::const_iterator	it = d.namemap.find(_recipient_name);
		
		if(it != d.namemap.end()){
			pool_idx = it->second;
		}else{
			if(d.config.isServerOnly()){
				edbgx(Debug::ipc, this<<" request for name resolve for a server only configuration");
				error.assign(-1, error.category());//TODO: server only
				return error;
			}
			
			return this->doSendMessageToNewPool(
				_recipient_name, _rmsgptr, msg_type_idx,
				_rresponse_fnc, _precipient_id_out, _pmsguid_out, _flags
			);
		}
	}else if(
		_rrecipient_id_in.poolid.index < d.conpooldq.size()
	){
		//we cannot check the uid right now because we need a lock on the session's mutex
		check_uid = true;
		pool_idx = _rrecipient_id_in.poolid.index;
		unique = _rrecipient_id_in.poolid.unique;
	}else{
		edbgx(Debug::ipc, this<<" recipient does not exist");
		error.assign(-1, error.category());//TODO: session does not exist
		return error;
	}
	
	Locker<Mutex>			lock2(d.connectionPoolMutex(pool_idx));
	ConnectionPoolStub 		&rconpool(d.conpooldq[pool_idx]);
	
	if(check_uid && rconpool.unique != unique){
		//failed uid check
		edbgx(Debug::ipc, this<<" connection pool does not exist");
		error.assign(-1, error.category());//TODO: connection pool does not exist
		return error;
	}
	
	if(rconpool.isStopping()){
		//failed uid check
		edbgx(Debug::ipc, this<<" connection pool is stopping");
		error.assign(-1, error.category());//TODO: connection pool is stopping
		return error;
	}
	
	if(_precipient_id_out){
		_precipient_id_out->poolid = ConnectionPoolId(pool_idx, rconpool.unique);
	}
	
	//At this point we can fetch the message from user's pointer
	//because from now on we can call complete on the message
	const MessageId msgid = rconpool.pushBackMessage(_rmsgptr, msg_type_idx, _rresponse_fnc, _flags);
	
	if(_pmsguid_out){
		
		MessageStub			&rmsgstub(rconpool.msgvec[msgidx]);
		
		rmsgstub.makeCancelable();
		
		*_pmsguid_out = msgid;
		idbgx(Debug::ipc, this<<" set message id to "<<*_pmsguid_out);
	}
	
	bool success = false;
	
	if(
		Message::is_synchronous(_flags) and
		rconpool.synchronous_connection_id.isValid()
	){
		success = manager().notify(
			rconpool.synchronous_connection_id,
			Connection::newMessageEvent()
		);
		if(not success){
			wdbgx(Debug::ipc, this<<" failed sending message to synch connection "<<rconpool.synchronous_connection_id);
			rconpool.synchronous_connection_id = ObjectIdT();
		}
	}

	while(not success and rconpool.conn_waitingq.size()){
		//a connection is waiting for something to send
		ObjectIdT 		objuid = rconpool.conn_waitingq.front();
		
		rconpool.conn_waitingq.pop();
		
		
		const bool		success = manager().notify(
			objuid,
			Connection::newMessageEvent()
		);
		
	}
	
	if(
		not success and
		rconpool.active_connection_count < d.config.max_per_pool_connection_count and
		rconpool.pending_resolve_count < d.config.max_per_pool_connection_count
	){
		
		idbgx(Debug::ipc, this<<" can create new connection in pool "<<rconpool.active_connection_count<<" pending connections "<< rconpool.pending_connection_count);
		
		DynamicPointer<aio::Object>		objptr(new Connection(ConnectionPoolId(pool_idx, rconpool.uid)));
			
		ObjectIdT						conuid = d.config.scheduler().startObject(objptr, *this, generic_event_category.event(GenericEvents::Start), error);
		
		if(!error){
			ResolveCompleteFunctionT		cbk(OnRelsolveF(manager(), conuid, Connection::resolveEvent()));
			
			d.config.name_resolve_fnc(rconpool.name, cbk);
			++rconpool.pending_connection_count;
			++rconpool.pending_resolve_count;
		}else{
			//there must be at least one connection to handle the message:
			cassert(rconpool.pending_connection_count + rconpool.active_connection_count);
		}
	}
	
	if(not success){
		wdbgx(Debug::ipc, this<<" no connection notified about the new message");
	}
	
	return error;
}

//-----------------------------------------------------------------------------

ErrorConditionT Service::doSendMessageToNewPool(
	const char *_recipient_name,
	MessagePointerT &_rmsgptr,
	const size_t _msg_type_idx,
	ResponseHandlerFunctionT &_rresponse_fnc,
	RecipientId *_precipient_id_out,
	MessageId *_pmsguid_out,
	ulong _flags
){
	solid::ErrorConditionT		error;
	size_t						pool_idx;
	
	if(d.conpoolcachestk.size()){
		pool_idx = d.conpoolcachestk.top();
		d.conpoolcachestk.pop();
	}else{
		pool_idx = this->doPushNewConnectionPool();
	}
	
	Locker<Mutex>					lock2(d.connectionPoolMutex(pool_idx));
	ConnectionPoolStub 				&rconpool(d.conpooldq[pool_idx]);
	
	rconpool.name = _recipient_name;
	
	DynamicPointer<aio::Object>		objptr(new Connection(ConnectionPoolId(pool_idx, rconpool.unique)));
	
	ObjectIdT						conuid = d.config.scheduler().startObject(objptr, *this, generic_event_category.event(GenericEvents::Start), error);
	
	if(error){
		edbgx(Debug::ipc, this<<" Starting Session: "<<error.message());
		rconpool.clear();
		d.conpoolcachestk.push(pool_idx);
		return error;
	}
	
	rconpool.msg_cancel_connection_id = conuid;
	
	d.namemap[rconpool.name.c_str()] = pool_idx;
	
	idbgx(Debug::ipc, this<<" Success starting Connection Pool object: "<<conuid.index<<','<<conuid.unique);
	
	//resolve the name
	ResolveCompleteFunctionT		cbk(OnRelsolveF(manager(), conuid, Connection::resolveEvent()));
	
	d.config.name_resolve_fnc(rconpool.name, cbk);
	
	MessageId msgid = rconpool.pushBackMessage(_rmsgptr, _msg_type_idx, _rresponse_fnc, _flags);
		
	++rconpool.pending_connection_count;
	++rconpool.pending_resolve_count;
	
	if(_precipient_id_out){
		_precipient_id_out->poolid = ConnectionPoolId(pool_idx, rconpool.uid);
	}
	
	if(_pmsguid_out){
		MessageStub	&rmsgstub = rconpool.msgvec[msgid.index];
		
		*_pmsguid_out = MessageId(msgid.index, rmsgstub.unique);
		rmsgstub.makeCancelable();
	}
	
	return error;
}
//-----------------------------------------------------------------------------
void Service::checkPoolForNewMessages(
	Connection &_rcon,
	aio::ReactorContext &_rctx,
	ObjectIdT const &_robjuid,
	const bool _connection_has_no_message_to_send
){
	Locker<Mutex>			lock2(d.connectionPoolMutex(_rcon.poolId().index));
	ConnectionPoolStub 		&rconpool(d.conpooldq[_rcon.poolId().index]);
	
	bool					connection_can_accept_more_message = true;
	
	cassert(rconpool.uid == _rcon.poolId().unique);
	if(rconpool.uid != _rcon.poolId().unique) return;
	
	if(rconpool.isMessageCancelConnectionStopping() and rconpool.msg_cancel_connection_id != _robjuid){
		idbgx(Debug::ipc, this<<' '<<&_rcon<<" switch message canceling connection from "<<rconpool.msg_cancel_connection_id<<" to "<<_robjuid);
		rconpool.msg_cancel_connection_id = _robjuid;
		rconpool.resetMessageCancelConnectionStopping();
	}
	
	if(_rcon.hasCompletingMessages()){
		//free-up positions of the completed messages
		auto free_up_fnc = [&rconpool](MessageId const &_rmsgid){
			rconpool.cacheMessageId(_rmsgid.index, _rmsgid.unique);
		};
		_rcon.visitCompletingMessages(free_up_fnc);
	}
	
	idbgx(Debug::ipc, this<<' '<<&_rcon<<" messages in pool: "<<rconpool.msgorder_inner_list.size());
	
	if(rconpool.msgorder_inner_list.size()){
		
		const size_t	msg_idx = rconpool.msgorder_inner_list.frontIndex();
		MessageStub		&rmsgstub = rconpool.msgvec[msg_idx];
		
		
		MessageId 		*pmsgid = nullptr;
		MessageId		pool_msg_id;
	
		//we have something to send
		if(
			Message::is_asynchronous(rmsgstub.msgbundle.message_flags) or 
			this->manager().id(_rcon) == rconpool.synchronous_connection_uid
		){
			
			if(rmsgstub.isCancelable()){
				pmsgid = &rmsgstub.msgid;
				pool_msg_id = MessageId(msg_idx, rmsgstub.unique);
				rmsgstub.objid = _robjuid;
				_rcon.directPushMessage(_rctx, rmsgstub.msgbundle, pmsgid, pool_msg_id);
				rconpool.popFrontMessage();
			}else{
				_rcon.directPushMessage(_rctx, rmsgstub.msgbundle, pmsgid, pool_msg_id);
				rconpool.cacheFrontMessage();
			}
		}else if(rconpool.synchronous_connection_uid.isInvalid()){
			
			if(rmsgstub.isCancelable()){
				pmsgid = &rmsgstub.msgid;
				pool_msg_id = MessageId(msg_idx, rmsgstub.unique);
				rmsgstub.objid = _robjuid;
				
				_rcon.directPushMessage(_rctx, rmsgstub.msgbundle, pmsgid, pool_msg_id);
			
				rconpool.synchronous_connection_uid = this->manager().id(_rcon);
				rconpool.popFrontMessage();
			}else{
				_rcon.directPushMessage(_rctx, rmsgstub.msgbundle, pmsgid, pool_msg_id);
			
				rconpool.synchronous_connection_uid = this->manager().id(_rcon);
				rconpool.cacheFrontMessage();
				rmsgstub.clear();
				cassert(rconpool.msgorder_inner_list.check());
			}
		}else{
			
			//worse case scenario - we must skip the current synchronous message
			//and find an asynchronous one
			size_t		qsz = rconpool.msgorder_inner_list.size();
			bool		found = false;
			
			while(qsz--){

				const size_t	crt_msg_idx = rconpool.msgorder_inner_list.frontIndex();
				MessageStub		&crt_msgstub(rconpool.msgvec[crt_msg_idx]);
				
				rconpool.msgorder_inner_list.popFront();
				
				if(not found and Message::is_asynchronous(crt_msgstub.msgbundle.message_flags)){
					
					found = true;
					
					if(crt_msgstub.isCancelable()){
						pmsgid = &crt_msgstub.msgid;
						pool_msg_id = MessageId(crt_msg_idx, rmsgstub.unique);
						crt_msgstub.objid = _robjuid;
						
						_rcon.directPushMessage(_rctx, crt_msgstub.msgbundle, pmsgid, pool_msg_id);
						crt_msgstub.clear();
					}else{
						_rcon.directPushMessage(_rctx, crt_msgstub.msgbundle, pmsgid, pool_msg_id);
						rconpool.msgcache_inner_list.pushBack(crt_msg_idx);
					}
					
				}else{
					rconpool.msgorder_inner_list.pushBack(crt_msg_idx);
					cassert(rconpool.msgorder_inner_list.check());
				}
			}
			idbgx(Debug::ipc, "msgorder_inner_list "<<rconpool.msgorder_inner_list);
			
			if(not found and _connection_has_no_message_to_send){
				rconpool.conn_waitingq.push(this->manager().id(_rcon));
			}
		
		}
		
	}
	
	if(not _rcon.isInPoolWaitingQueue() and (_connection_has_no_message_to_send or connection_can_accept_more_message)){
		rconpool.conn_waitingq.push(_robjuid);
		_rcon.setInPoolWaitingQueue();
	}
	
	if(_connection_has_no_message_to_send){
		rconpool.conn_waitingq.push(this->manager().id(_rcon));
	}
	
}

//-----------------------------------------------------------------------------
ErrorConditionT Service::doNotifyConnectionNewMessage(
	const RecipientId	&_rrecipient_id_in,
	MessagePointerT &_rmsgptr,
	const size_t _msg_type_idx,
	ResponseHandlerFunctionT &_rresponse_fnc,
	MessageId *_pmsgid_out,
	ulong _flags
){
	//d.mtx must be locked
	
	if(_rrecipient_id_in.isValidPool()){
		return error_connection_inexistent;//TODO: more explicit error
	}
	
	const size_t	pool_idx = _rrecipient_id_in.poolId().index;
	
	if(pool_idx >= d.conpooldq.size() or d.conpooldq[pool_idx].unique != _rrecipient_id_in.poolId().unique){
		return error_connection_inexistent;//TODO: more explicit error
	}
	
	Locker<Mutex>				lock2(d.connectionPoolMutex(pool_idx));
	ConnectionPoolStub			&rconpool = d.conpooldq[pool_idx];
	solid::ErrorConditionT		error;
	
	const bool					is_server_side_pool = rconpool.name.empty();//unnamed pool has a single connection
	
	MessageId 					msgid;
	
	bool						success = false;
	
	if(is_server_side_pool){
		//push the message into rconpool.msgorder_inner_list
		//
		msgid = rconpool.pushBackMessage(_rmsgptr, _msg_type_idx, _rresponse_fnc, _flags);
		success = manager().notify(
			_rrecipient_id_in.connectionId(),
			Connection::newMessageEvent()
		);
	}else{
		msgid = rconpool.insertMessage(_rmsgptr, _msg_type_idx, _rresponse_fnc, _flags);
		success = manager().notify(
			_rrecipient_id_in.connectionId(),
			Connection::newMessageEvent(msgid)
		);
	}
	
	const bool					success = manager().notify(
		_rrecipient_id_in.connectionId(),
		is_server_side_pool ? Connection::newMessageEvent() : Connection::newMessageEvent(msgid)
	);
	
	if(success){
		if(_pmsgid_out){
			*_pmsgid_out = msgid;
		}
	}else if(is_server_side_pool){
		rconpool.clearPopAndCacheMessage(msgid.index);
		error = error_connection_inexistent;//TODO: more explicit error
	}else{
		rconpool.clearAndCacheMessage(msgid.index);
		error = error_connection_inexistent;//TODO: more explicit error
	}
	
	return error;
}
//-----------------------------------------------------------------------------
ErrorConditionT Service::doNotifyConnectionDelayedClose(
	ObjectIdT const &_robjuid
){
	ErrorConditionT						error;
	DelayedCloseConnectionVisitorF		fnc(*this, error);
	const bool 							success = manager().visit(_robjuid, fnc);
	
	if(!success and not error){
		error = error_connection_inexistent;
	}
	return error;
}
//-----------------------------------------------------------------------------
ErrorConditionT Service::delayedConnectionClose(
	RecipientId const &_rconnection_uid
){
	return doNotifyConnectionDelayedClose(_rconnection_uid.connectionid);
}
//-----------------------------------------------------------------------------
ErrorConditionT Service::forcedConnectionClose(
	RecipientId const &_rconnection_uid
){
	ErrorConditionT	error;
	const bool		success = manager().notify(_rconnection_uid.connectionid, generic_event_category.event(GenericEvents::Kill));
	if(not success){
		error = error_connection_inexistent;
	}
	return error;
}
//-----------------------------------------------------------------------------
ErrorConditionT Service::cancelMessage(RecipientId const &_rconnection_uid, MessageId const &_rmsguid){
	ErrorConditionT		error;
	
	if(_rconnection_uid.isValidPool()){
		//cancel a message from within a connection pool
		const size_t			pool_idx = _rconnection_uid.poolId().index;
		Locker<Mutex>			lock(d.mtx);
		Locker<Mutex>			lock2(d.connectionPoolMutex(pool_idx));
		ConnectionPoolStub 		&rconpool(d.conpooldq[pool_idx]);
		
		if(_rmsguid.index < rconpool.msgvec.size() and rconpool.msgvec[_rmsguid.index].unique == _rmsguid.unique){
			MessageStub	&rmsgstub = rconpool.msgvec[_rmsguid.index];
			
			rmsgstub.msgbundle.message_flags |= Message::CanceledFlagE;
			
			if(rmsgstub.msgid.isValid()){
				//message handled by a connection
				CancelMessageConnectionVistiorF		fnc(*this, error, rmsgstub.msgid);
		
				bool								success = manager().visit(rmsgstub.objid, fnc);
				
				if(success){
					cassert(not error);
				}else if(not error){
					THROW_EXCEPTION("Logic error: lost message");
					error = error_connection_inexistent;
				}
			}else if(rmsgstub.msgbundle.message_ptr.get()){
				//message waiting to be handled by a connection
				//send it to msg_cancel_connection_id
				solid::ErrorConditionT					error;
				PushCanceledMessageConnectionVisitorF	fnc(*this, rmsgstub.msgbundle, error, _rmsguid);
				bool									success = manager().visit(rconpool.msg_cancel_connection_id, fnc);
				
				if(success){
					cassert(not error);
					//message successfully delivered
				}else if(not error){
					//we do nothing - the message will be fetched 
					//by a connection
				}
			}else{
				THROW_EXCEPTION("Logic error: lost message");
				error.assign(-1, error.category());//message does not exist
			}
			
		}else{
			error.assign(-1, error.category());//message does not exist
		}
		
	}else if(_rconnection_uid.isValidConnection()){
		//cancel a message from within a connection without pool (server side connection)
		CancelMessageConnectionVistiorF		fnc(*this, error, _rmsguid);
		
		bool								success = manager().visit(_rconnection_uid.connectionId(), fnc);
		if(success){
			cassert(not error);
		}else if(not error){
			error = error_connection_inexistent;
		}
	}else{
		error.assign(-1, error.category());//invalid connectionuid
	}
	return error;
}
//-----------------------------------------------------------------------------
// Three situations in doActivateConnection is called:
// 1. for a new server connection - in a client-server scenario
// 2. for a new server connection - in a peer2peer scenario - given a valid _recipient_name
// 3. for a new client connection - in any scenario - given a valid _rconnection_uid.poolid
//
// 

ErrorConditionT Service::doActivateConnection(
	RecipientId const &_rconnection_uid,
	const char *_recipient_name,
	ActivateConnectionMessageFactoryFunctionT const &_rmsgfactory,
	const bool _may_quit
){
	solid::ErrorConditionT					err;
	std::pair<MessagePointerT, uint32>		msgpair;
	ConnectionPoolId						poolid;
	
	if(_recipient_name == nullptr and not _rconnection_uid.poolid.isValid()){//situation 1
		bool accepted_connection = false;
		err = doNotifyConnectionActivate(_rconnection_uid.connectionid, poolid, accepted_connection);
		return err;
	}
	
	if(_recipient_name != nullptr and _rconnection_uid.poolid.isValid()){
		edbgx(Debug::ipc, this<<" only accepted connections allowed");
		err.assign(-1, err.category());//TODO: only accepted connections allowed
		return err;
	}
	
	Locker<Mutex>				lock(d.mtx);
	SmartLocker<Mutex>			lock2;
	
	if(_rconnection_uid.poolid.isValid()){
		poolid = _rconnection_uid.poolid;//situation 3
	}else{
		//situation 2
		NameMapT::const_iterator	it = d.namemap.find(_recipient_name);
		
		if(it != d.namemap.end()){//connection pool exist
			poolid.index = it->second;
			SmartLocker<Mutex>		tmplock(d.connectionPoolMutex(poolid.index));
			ConnectionPoolStub 		&rconpool(d.conpooldq[poolid.index]);
			
			poolid.unique = rconpool.uid;
			
			lock2 = std::move(tmplock);
		}else{//connection pool does not exist
			
			if(d.config.isServerOnly()){
				edbgx(Debug::ipc, this<<" request for name resolve for a server only configuration");
				err.assign(-1, err.category());//TODO: server only
				return err;
			}
			
			if(d.conpoolcachestk.size()){
				poolid.index = d.conpoolcachestk.top();
				d.conpoolcachestk.pop();
			}else{
				poolid.index = this->doPushNewConnectionPool();
			}
			
			SmartLocker<Mutex>				tmplock(d.connectionPoolMutex(poolid.index));
			ConnectionPoolStub 				&rconpool(d.conpooldq[poolid.index]);
			
			rconpool.name = _recipient_name;
			poolid.unique = rconpool.uid;
		
			d.namemap[rconpool.name.c_str()] = poolid.index;
			
			lock2 = std::move(tmplock);
		}
	}
	
	ConnectionPoolStub			&rconpool(d.conpooldq[poolid.index]);
	const size_t				wouldbe_active_connection_count = rconpool.active_connection_count + 1;
	ResponseHandlerFunctionT	response_handler;
	
	poolid.unique = rconpool.uid;
	
	
	if(
		(
			wouldbe_active_connection_count < d.config.max_per_pool_connection_count
		) or
		(
			(
				wouldbe_active_connection_count == d.config.max_per_pool_connection_count
			) and not rconpool.pending_connection_count
		) or
		(
			(
				wouldbe_active_connection_count == d.config.max_per_pool_connection_count
			) and rconpool.pending_connection_count and not _may_quit
		)
	){
		std::pair<MessagePointerT, uint32>			msgpair;
		bool										success = false;
		bool										accepted_connection = false;
		
		idbgx(Debug::ipc, this<<" connection count limit not reached on connection-pool: "<<poolid);
		
		if(not FUNCTION_EMPTY(_rmsgfactory)){
			msgpair = _rmsgfactory(err);
		}
		
		if(not msgpair.first.empty()){
			const size_t		msg_type_idx = tm.index(msgpair.first.get());
			
			if(msg_type_idx != 0){
				//first send the Init message
				err = doNotifyConnectionPushMessage(
					_rconnection_uid.connectionid,
					msgpair.first,
					msg_type_idx,
					response_handler,
// 					poolid,
// 					nullptr,//TODO: revisit
					nullptr, //TODO: revisit
					msgpair.second
				);
				if(err){
					success = false;
				}else{
					//then send the activate event
					err = doNotifyConnectionActivate(_rconnection_uid.connectionid, poolid, accepted_connection);
					success = !err;
				}
			}else{
				err.assign(-1, err.category());//TODO: Unknown message type
				success = false;
			}
		}else if(FUNCTION_EMPTY(_rmsgfactory)){
			err = doNotifyConnectionActivate(_rconnection_uid.connectionid, poolid, accepted_connection);
			success = !err;
		}else{
			//close connection
			doNotifyConnectionDelayedClose(_rconnection_uid.connectionid);
			success = false;
			err.assign(-1, err.category());//TODO: 
		}
		
		if(success){
			++rconpool.active_connection_count;
			if(not accepted_connection){
				--rconpool.pending_connection_count;
			}
			idbgx(Debug::ipc, this<<' '<<poolid<<" active_connection_count "<<rconpool.active_connection_count<<" pending_connection_count "<<rconpool.pending_connection_count);
		}
	}else{
		idbgx(Debug::ipc, this<<" connection count limit reached on connection-pool: "<<poolid);
		
		err.assign(-1, err.category());//TODO: Connection count limit
		
		std::pair<MessagePointerT, uint32>			msgpair;
		
		
		if(not FUNCTION_EMPTY(_rmsgfactory)){
			msgpair = _rmsgfactory(err);
		}
		
		if(not msgpair.first.empty()){
			const size_t		msg_type_idx = tm.index(msgpair.first.get());
			
			if(msg_type_idx != 0){
				doNotifyConnectionPushMessage(
					_rconnection_uid.connectionid,
					msgpair.first,
					msg_type_idx,
					response_handler,
// 					poolid,
// 					nullptr, //TODO:revisit
					nullptr,//TODO: revisit
					msgpair.second
				);
			}
		}
		
		msgpair.first.clear();
		msgpair.second = 0;
		//close connection
		doNotifyConnectionDelayedClose(_rconnection_uid.connectionid);
		cassert(rconpool.active_connection_count or rconpool.pending_connection_count);
	}
	
	return err;
}
//-----------------------------------------------------------------------------
void Service::activateConnectionComplete(Connection &_rcon){
	if(_rcon.conpoolid.isValid()){
	
		Locker<Mutex>		lock2(d.connectionPoolMutex(_rcon.conpoolid.index));
		ConnectionPoolStub	&rconpool(d.conpooldq[_rcon.conpoolid.index]);
		
		idbgx(Debug::ipc, this<<' '<<_rcon.conpoolid<<" active_connection_count "<<rconpool.active_connection_count<<" pending_connection_count "<<rconpool.pending_connection_count);
	}
}
//-----------------------------------------------------------------------------
void Service::onConnectionClose(Connection &_rcon, aio::ReactorContext &_rctx, ObjectIdT const &_robjuid){
	
	idbgx(Debug::ipc, this<<' '<<&_rcon<<" "<<_robjuid);
	
	if(_rcon.poolId().isValid()){
		ConnectionPoolId	conpoolid = _rcon.conpoolid;
		Locker<Mutex>		lock(d.mtx);
		Locker<Mutex>		lock2(d.connectionPoolMutex(_rcon.conpoolid.index));
		const size_t 		conpoolindex = _rcon.conpoolid.index;
		ConnectionPoolStub	&rconpool(d.conpooldq[conpoolindex]);
		
		_rcon.conpoolid = ConnectionPoolId();
		
		cassert(rconpool.uid == conpoolid.unique);
		
		idbgx(Debug::ipc, this<<' '<<_robjuid);
		
		if(_rcon.isAtomicActive()){
			--rconpool.active_connection_count;
			idbgx(Debug::ipc, this<<' '<<conpoolid<<" active_connection_count "<<rconpool.active_connection_count<<" pending_connection_count "<<rconpool.pending_connection_count);
			//we do not actually need to pop the connection from conn_waitingq
			//doPushMessageToConnection will fail anyway
#if 0
			if(_robjuid.isValid()){
				//pop the connection from waitingq
				size_t	sz = rconpool.conn_waitingq.size();
				while(sz){
					if(
						rconpool.conn_waitingq.front().index != _robjuid.index /*and
						rconpool.conn_waitingq.front().unique == _robjuid.unique*/
					){
						rconpool.conn_waitingq.push(rconpool.conn_waitingq.front());
					}else{
						cassert(rconpool.conn_waitingq.front().unique == _robjuid.unique);
					}
					
					rconpool.conn_waitingq.pop();
					
					--sz;
				}
			}
#endif
		}else{
			cassert(not _rcon.isServer());
			--rconpool.pending_connection_count;

			idbgx(Debug::ipc, this<<' '<<conpoolid<<" active_connection_count "<<rconpool.active_connection_count<<" pending_connection_count "<<rconpool.pending_connection_count);
		}
		
		if(rconpool.synchronous_connection_uid == _robjuid){
			rconpool.synchronous_connection_uid = ObjectIdT::invalid();
		}
		
		if((rconpool.active_connection_count + rconpool.pending_connection_count) == 0){
			//_rcon is the last dying connection
			//so, move all pending messages to _rcon for completion
			while(rconpool.msgorder_inner_list.size()){
				MessageStub 	&rmsgstub = rconpool.msgorder_inner_list.front();
				MessageId		pool_msg_id(rconpool.msgorder_inner_list.frontIndex(), rmsgstub.unique);
				_rcon.directPushMessage(_rctx, rmsgstub.msgbundle, nullptr, pool_msg_id, /*_force =*/ true);
				
				rconpool.cacheFrontMessage();
			}
			
			idbgx(Debug::ipc, "msgorder_inner_list "<<rconpool.msgorder_inner_list);
			
			d.conpoolcachestk.push(conpoolid.index);
		
			rconpool.clear();
		}else{
			doFetchUnsentMessagesFromConnection(_rcon, _rctx, conpoolindex);
		}
	}
}
//-----------------------------------------------------------------------------
void Service::doFetchUnsentMessagesFromConnection(
	Connection &_rcon, aio::ReactorContext &_rctx, const size_t _conpoolindex
){
	ConnectionPoolStub	&rconpool(d.conpooldq[_conpoolindex]);
	size_t				qsz = rconpool.msgorder_inner_list.size();
			
	_rcon.fetchUnsentMessages(
		[this](
			ConnectionPoolId &_rconpoolid,
			MessageBundle &_rmsgbundle,
			MessageId const &_rmsgid
		){
			this->pushBackMessageToConnectionPool(_rconpoolid, _rmsgbundle, _rmsgid);
		}
	);
	
	if(rconpool.msgorder_inner_list.size() > qsz){
		//move the newly pushed messages up-front of msgorder_inner_list.
		//rotate msgorder_inner_list by qsz
		while(qsz--){
			const size_t	idx{rconpool.msgorder_inner_list.frontIndex()};
			
			rconpool.msgorder_inner_list.popFront();
			rconpool.msgorder_inner_list.pushBack(idx);
		}
		idbgx(Debug::ipc, "msgorder_inner_list "<<rconpool.msgorder_inner_list);
		cassert(rconpool.msgorder_inner_list.check());
	}
}
//-----------------------------------------------------------------------------
void Service::pushBackMessageToConnectionPool(
	ConnectionPoolId &_rconpoolid,
	MessageBundle &_rmsgbundle,
	MessageId const &_rmsgid
){
	ConnectionPoolStub	&rconpool(d.conpooldq[_rconpoolid.index]);
	
	cassert(rconpool.uid == _rconpoolid.unique);
	
	if(
		Message::is_idempotent(_rmsgbundle.message_flags) or 
		(/*not Message::is_started_send(_flags) and*/ not Message::is_done_send(_rmsgbundle.message_flags))
	){
		_rmsgbundle.message_flags &= ~(Message::DoneSendFlagE | Message::StartedSendFlagE);
		
		rconpool.pushBackMessage(
			_rmsgbundle.message_ptr,
			_rmsgbundle.message_type_id,
			_rmsgbundle.response_fnc,
			_rmsgbundle.message_flags
		);
	}
}
//-----------------------------------------------------------------------------
void Service::acceptIncomingConnection(SocketDevice &_rsd){
	ConnectionPoolId	poolid;
	
	{
		//allocate a new pool
		Locker<Mutex>				lock(d.mtx);
		
		if(d.conpoolcachestk.size()){
			poolid.index = d.conpoolcachestk.top();
			d.conpoolcachestk.pop();
		}else{
			poolid.index = this->doPushNewConnectionPool();
		}
		
		Locker<Mutex>				lock2(d.connectionPoolMutex(poolid.index));
		ConnectionPoolStub 			&rconpool(d.conpooldq[poolid.index]);
		poolid.unique = rconpool.unique;
		rconpool.pending_connection_count = 1;
	}
	
	DynamicPointer<aio::Object>		objptr(new Connection(_rsd, poolid));
	solid::ErrorConditionT			err;
	ObjectIdT						conuid = d.config.scheduler().startObject(
		objptr, *this, generic_event_category.event(GenericEvents::Start), err
	);
	
	idbgx(Debug::ipc, this<<" receive connection ["<<conuid<<"] err = "<<err.message());
	
	if(err){
		cassert(conuid.isInvalid());
		Locker<Mutex>				lock(d.mtx);
		Locker<Mutex>				lock2(d.connectionPoolMutex(poolid.index));
		ConnectionPoolStub 			&rconpool(d.conpooldq[poolid.index]);
		rconpool.clear();
		d.conpoolcachestk.push(poolid.index);
	}else{
		cassert(conuid.isValid());
	}
}
//-----------------------------------------------------------------------------
void Service::onIncomingConnectionStart(ConnectionContext &_rconctx){
	configuration().incoming_connection_start_fnc(_rconctx);
}
//-----------------------------------------------------------------------------
void Service::onOutgoingConnectionStart(ConnectionContext &_rconctx){
	configuration().outgoing_connection_start_fnc(_rconctx);
}
//-----------------------------------------------------------------------------
void Service::onConnectionStop(ConnectionContext &_rconctx, ErrorConditionT const &_err){
	configuration().connection_stop_fnc(_rconctx, _err);
}
//-----------------------------------------------------------------------------
ulong Service::onConnectionWantStop(Connection &_rcon, aio::ReactorContext &_rctx, ObjectIdT const &_robjuid){
	if(_rcon.poolId().isValid()){
		Locker<Mutex>			lock2(d.connectionPoolMutex(_rcon.poolId().index));
		const size_t 			conpoolindex = _rcon.poolId().index;
		ConnectionPoolStub 		&rconpool(d.conpooldq[conpoolindex]);
		
		cassert(rconpool.uid == _rcon.poolId().unique);
		if(rconpool.uid != _rcon.poolId().unique) return 0;
		
		if(rconpool.msg_cancel_connection_id == _robjuid){
			cassert(configuration().msg_cancel_connection_wait_seconds != 0);
			
			rconpool.setMessageCancelConnectionStopping();
			
			//TODO: maybe we can use rconpool.conn_waitingq to 
			//replace the msg_cancel_connection_id with another connectionid
			
			if((rconpool.active_connection_count + rconpool.pending_connection_count) != 1){
				//only call doFetchUnsentMessagesFromConnection 
				//for connections that will not stop right away
				
				doFetchUnsentMessagesFromConnection(_rcon, _rctx, conpoolindex);
				
				//the connection is used for message canceling
				return configuration().msg_cancel_connection_wait_seconds;
			}else{
				//this is the last connection
				rconpool.setStopping();
			}
		}
	}
	return 0;
}
//-----------------------------------------------------------------------------
void Service::forwardResolveMessage(ConnectionPoolId const &_rconpoolid, Event &_revent){

	ResolveMessage 	*presolvemsg = _revent.any().cast<ResolveMessage>();
	ErrorConditionT	err;
	
	++presolvemsg->crtidx;
	
	if(presolvemsg->crtidx < presolvemsg->addrvec.size()){
		DynamicPointer<aio::Object>		objptr(new Connection(_rconpoolid));
		
		Locker<Mutex>					lock(d.connectionPoolMutex(_rconpoolid.index));
		ConnectionPoolStub				&rconpool(d.conpooldq[_rconpoolid.index]);
		
		ObjectIdT	conuid = d.config.scheduler().startObject(objptr, *this, std::move(_revent), err);
		
		if(!err){
			++rconpool.pending_connection_count;
			++rconpool.pending_resolve_count;
			
			idbgx(Debug::ipc, this<<' '<<_rconpoolid<<" new connection "<<conuid<<" - active_connection_count "<<rconpool.active_connection_count<<" pending_connection_count "<<rconpool.pending_connection_count);
		}else{
			idbgx(Debug::ipc, this<<' '<<conuid<<" "<<err.message());
		}
	}else{
		Locker<Mutex>					lock(d.connectionPoolMutex(_rconpoolid.index));
		ConnectionPoolStub				&rconpool(d.conpooldq[_rconpoolid.index]);
		--rconpool.pending_resolve_count;
	}
}
//=============================================================================
//-----------------------------------------------------------------------------
/*virtual*/ Message::~Message(){
	
}
//-----------------------------------------------------------------------------
//=============================================================================
struct ResolveF{
	ResolveCompleteFunctionT	cbk;
	
	void operator()(ResolveData &_rrd, ERROR_NS::error_code const &_rerr){
		AddressVectorT		addrvec;
		if(!_rerr){
			for(auto it = _rrd.begin(); it != _rrd.end(); ++it){
				addrvec.push_back(SocketAddressStub(it));
				idbgx(Debug::ipc, "add resolved address: "<<addrvec.back());
			}
		}
		cbk(addrvec);
	}
};

void ResolverF::operator()(const std::string&_name, ResolveCompleteFunctionT& _cbk){
	std::string		tmp;
	const char 		*hst_name;
	const char		*svc_name;
	
	size_t off = _name.rfind(':');
	if(off != std::string::npos){
		tmp = _name.substr(0, off);
		hst_name = tmp.c_str();
		svc_name = _name.c_str() + off + 1;
		if(!svc_name[0]){
			svc_name = default_service.c_str();
		}
	}else{
		hst_name = _name.c_str();
		svc_name = default_service.c_str();
	}
	
	ResolveF		fnc;
	
	fnc.cbk = std::move(_cbk);
	
	rresolver.requestResolve(fnc, hst_name, svc_name, 0, this->family, SocketInfo::Stream);
}
//=============================================================================

std::ostream& operator<<(std::ostream &_ros, RecipientId const &_con_id){
	_ros<<'{'<<_con_id.connectionId()<<"}{"<<_con_id.poolId()<<'}';
	return _ros;
}
//-----------------------------------------------------------------------------
std::ostream& operator<<(std::ostream &_ros, RequestId const &_msguid){
	_ros<<'{'<<_msguid.index<<','<<_msguid.unique<<'}';
	return _ros;
}
//-----------------------------------------------------------------------------
std::ostream& operator<<(std::ostream &_ros, MessageId const &_msguid){
	_ros<<'{'<<_msguid.index<<','<<_msguid.unique<<'}';
	return _ros;
}
//=============================================================================
}//namespace ipc
}//namespace frame
}//namespace solid

