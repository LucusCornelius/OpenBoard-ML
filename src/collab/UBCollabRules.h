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

#ifndef UBCOLLABRULES_H_
#define UBCOLLABRULES_H_

#include "UBCollabWire.h"

/**
 * The register of an item (or a page): the incarnation is the version of the
 * add that made it exist, the version that of its last change.
 */
struct UBCollabRegister
{
    UBCollabVersion incarnation;
    UBCollabVersion version;
    bool alive{true};

    bool operator==(const UBCollabRegister& other) const
    {
        return incarnation == other.incarnation && version == other.version && alive == other.alive;
    }
};


/**
 * What an operation of someone else does to an item, from its register here.
 * The same decision on every computer, in whatever order the operations
 * arrive, makes them end in the same state:
 *   - an operation of an older incarnation is ignored;
 *   - within an incarnation a removal wins over every change, of two changes
 *     the higher version wins;
 *   - an operation of a newer incarnation replaces what is here (an item that
 *     comes back, or the first news of an item whose add was not seen yet).
 * Operations that come again (the same version) change nothing.
 */
class UBCollabRules
{
    public:
        enum Action
        {
            Ignore,     // nothing changes
            Store,      // only the register changes (a later removal of a removed item)
            Remove,     // the item goes, the register is stored
            Apply       // the item gets the content of the operation, the register is stored
        };

        struct Decision
        {
            Action action{Ignore};
            UBCollabRegister reg;
        };

        /**
         * hasRegister: there is a register for the item here; reg is it. exists: the item is
         * on the page (an item of the initial board has no register). op is add, change or
         * remove (UBCollabProtocol::ItemOpKind). authoritative: the operation comes with a page
         * of the host, whose registers are taken as they are.
         */
        static Decision decide(bool hasRegister, const UBCollabRegister& reg, bool exists, quint8 op,
                               const UBCollabVersion& incarnation, const UBCollabVersion& version, bool authoritative);
};

#endif /* UBCOLLABRULES_H_ */
