/*
 * Copyright (C) 2026 OpenBoard-ML contributors
 *
 * This file is part of OpenBoard.
 *
 * OpenBoard is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License,
 * with a specific linking exception for the OpenSSL project's
 * "OpenSSL" library (or with modified versions of it that use the
 * same license as the "OpenSSL" library).
 *
 * OpenBoard is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenBoard. If not, see <http://www.gnu.org/licenses/>.
 */

#include "UBCollabRules.h"

#include "UBCollabProtocol.h"

#include "core/memcheck.h"

UBCollabRules::Decision UBCollabRules::decide(bool hasRegister, const UBCollabRegister& reg, bool exists, quint8 op,
                                              const UBCollabVersion& incarnation, const UBCollabVersion& version,
                                              bool authoritative)
{
    Decision decision;
    decision.reg.incarnation = incarnation;
    decision.reg.version = version;
    decision.reg.alive = op != UBCollabProtocol::RemoveItem;

    // an item of the initial board has no register: it is alive in the first incarnation
    const bool known = hasRegister || exists;
    const UBCollabRegister here = hasRegister ? reg : UBCollabRegister{UBCollabVersion(), UBCollabVersion(), exists};

    if (authoritative)
    {
        // the page of the host: its registers as they are, nothing to do when they are here already
        if (hasRegister && here == decision.reg && exists == decision.reg.alive)
            return Decision{Ignore, here};

        if (!hasRegister && incarnation.isZero() && version.isZero() && exists && decision.reg.alive)
            return Decision{Ignore, here};

        decision.action = decision.reg.alive ? Apply : Remove;
        return decision;
    }

    // an operation of an incarnation that was replaced already
    if (known && incarnation < here.incarnation)
        return Decision{Ignore, here};

    const bool sameIncarnation = known && incarnation == here.incarnation;

    if (sameIncarnation && !here.alive)
    {
        // removed in this incarnation: the removal wins over every change
        if (op == UBCollabProtocol::RemoveItem && version > here.version)
        {
            UBCollabRegister dead = here;
            dead.version = version;
            return Decision{Store, dead};
        }

        return Decision{Ignore, here};
    }

    // A removal: the register keeps the version of the removal, not that of a change that may have
    // come before it here; other computers may have got that change after the removal and ignored
    // it. So the register of a removed item is the highest removal of its incarnation everywhere.
    if (op == UBCollabProtocol::RemoveItem)
    {
        decision.action = Remove;
        return decision;
    }

    // a change that is not newer than what is here (also the same operation again)
    if (sameIncarnation && version <= here.version)
        return Decision{Ignore, here};

    decision.action = Apply;
    return decision;
}
