// solid/frame/object.cpp
//
// Copyright (c) 2015 Valentin Palade (vipalade @ gmail . com)
//
// This file is part of SolidFrame framework.
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt.
//
#include "solid/system/cassert.hpp"

#include "solid/frame/completion.hpp"
#include "solid/frame/manager.hpp"
#include "solid/frame/object.hpp"
#include "solid/frame/reactor.hpp"
#include "solid/frame/reactorcontext.hpp"
#include "solid/frame/service.hpp"

namespace solid {
namespace frame {
//---------------------------------------------------------------------
//----  Object  ----
//---------------------------------------------------------------------

Object::Object() {}

/*virtual*/ void Object::onEvent(ReactorContext& /*_rctx*/, Event&& /*_uevent*/)
{
}

bool Object::isRunning() const
{
    return runId().isValid();
}

bool Object::registerCompletionHandler(CompletionHandler& _rch)
{
    _rch.pnext = this->pnext;
    if (_rch.pnext != nullptr) {
        _rch.pnext->pprev = &_rch;
    }
    this->pnext = &_rch;
    _rch.pprev  = this;
    return isRunning();
}

void Object::registerCompletionHandlers()
{
    CompletionHandler* pch = this->pnext;

    while (pch != nullptr) {
        pch->activate(*this);
        pch = pch->pnext;
    }
}

bool Object::doPrepareStop(ReactorContext& _rctx)
{
    if (this->disableVisits(_rctx.service().manager())) {
        CompletionHandler* pch = this->pnext;

        while (pch != nullptr) {
            pch->pprev = nullptr; //unregister
            pch->deactivate();

            pch = pch->pnext;
        }
        this->pnext = nullptr;
        return true;
    }
    return false;
}

//---------------------------------------------------------------------
//----  ObjectBase      ----
//---------------------------------------------------------------------

ObjectBase::ObjectBase()
    : fullid(static_cast<IndexT>(InvalidIndex()))
{
}

void ObjectBase::unregister(Manager& _rm)
{
    if (isRegistered()) {
        _rm.unregisterObject(*this);
        fullid.store(InvalidIndex());
    }
}

/*NOTE:
    Disable visit i.e. ensures that after this call, the manager will
    not permit any visits (and  implicitly no event notification) on the object.
    If the manager’s visit method manages to acquire object’s lock after
    disableVisit terminates, the visit will not take place.
    If the visit happens before disableVisit acquires Lock on the object,
    and an event is delivered to object, the reactor handling the object,
    must ensure that the object receives the event before stopping
    the object:
        it does that by not stopping the object right away,
        and instead reposting the stop to ensure that all incoming events
        are fetched and delivered.
        This works because disableVisits is only called on the Reactor thread.
*/

bool ObjectBase::disableVisits(Manager& _rm)
{
    return _rm.disableObjectVisits(*this);
}

ObjectBase::~ObjectBase()
{
}

/*virtual*/ void ObjectBase::onStop(Manager& /*_rm*/)
{
}
void ObjectBase::stop(Manager& _rm)
{
    onStop(_rm);
    unregister(_rm);
}

} //namespace frame
} //namespace solid
