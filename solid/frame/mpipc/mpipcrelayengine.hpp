// solid/frame/mpipc/mpipcrelayengine.hpp
//
// Copyright (c) 2017 Valentin Palade (vipalade @ gmail . com)
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//

#pragma once

#include "solid/frame/mpipc/mpipcconfiguration.hpp"
#include "solid/system/error.hpp"
#include "solid/system/pimpl.hpp"

namespace solid {
namespace frame {
namespace mpipc {

struct RelayStub {
    RelayData  data_;
    RelayStub* pnext;
};

class RelayEngine : public RelayEngineBase {
public:
    RelayEngine(Manager& _rm);
    ~RelayEngine();

    void connectionRegister(const ObjectIdT& _rconuid, std::string&& _uname);

    void debugDump();

protected:
    void connectionStop(const ObjectIdT& _rconuid) override;

    bool doRelayStart(
        const ObjectIdT& _rconuid,
        MessageHeader&   _rmsghdr,
        RelayData&&      _rrelmsg,
        MessageId&       _rrelay_id,
        ErrorConditionT& _rerror) override;

    bool doRelayResponse(
        const ObjectIdT& _rconuid,
        MessageHeader&   _rmsghdr,
        RelayData&&      _rrelmsg,
        const MessageId& _rrelay_id,
        ErrorConditionT& _rerror) override;

    bool doRelay(
        const ObjectIdT& _rconuid,
        RelayData&&      _rrelmsg,
        const MessageId& _rrelay_id,
        ErrorConditionT& _rerror) override;

    void doComplete(
        const ObjectIdT& _rconuid,
        RelayData*       _prelay_data,
        MessageId const& _rengine_msg_id,
        bool&            _rmore) override;

    void doCancel(
        const ObjectIdT& _rconuid,
        RelayData*       _prelay_data,
        MessageId const& _rengine_msg_id,
        DoneFunctionT&   _done_fnc) override;

    void doPollNew(const ObjectIdT& _rconuid, PushFunctionT& _try_push_fnc, bool& _rmore) override;
    void doPollDone(const ObjectIdT& _rconuid, DoneFunctionT& _done_fnc, CancelFunctionT& _cancel_fnc) override;

    size_t doRegisterConnection(std::string&& _uname);
    size_t doRegisterConnection(const ObjectIdT& _rconuid);

private:
    struct Data;
    PimplT<Data> impl_;
};

} //namespace mpipc
} //namespace frame
} //namespace solid
