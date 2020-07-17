/*
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2010-2015 Rising Gods <http://www.rising-gods.de/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "blackrock_spire.h"

enum Spells
{
    SPELL_SHOOT                     = 16496,
    SPELL_STUNBOMB                  = 16497,
    SPELL_HEALING_POTION            = 15504,
    SPELL_HOOKEDNET                 = 15609
};

enum Events
{
    EVENT_SHOOT                     = 1,
    EVENT_STUN_BOMB                 = 2
};

class quartermaster_zigris : public CreatureScript
{
    public:
        quartermaster_zigris() : CreatureScript("quartermaster_zigris") { }
    
        struct boss_quatermasterzigrisAI : public BossAI
        {
            boss_quatermasterzigrisAI(Creature* creature) : BossAI(creature, DATA_QUARTERMASTER_ZIGRIS) { }
    
            void EnterCombat(Unit* who) override
            {
                BossAI::EnterCombat(who);
                events.ScheduleEvent(EVENT_SHOOT,      1 * IN_MILLISECONDS);
                events.ScheduleEvent(EVENT_STUN_BOMB, 16 * IN_MILLISECONDS);
            }
    
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;
    
                events.Update(diff);
    
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;
    
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_SHOOT:
                            DoCastVictim(SPELL_SHOOT);
                            events.ScheduleEvent(EVENT_SHOOT, 0.5 * IN_MILLISECONDS);
                            break;
                        case EVENT_STUN_BOMB:
                            DoCastVictim(SPELL_STUNBOMB);
                            events.ScheduleEvent(EVENT_STUN_BOMB, 14 * IN_MILLISECONDS);
                            break;
                    }
                }
                
                DoMeleeAttackIfReady();
            }
        };
    
        CreatureAI* GetAI(Creature* creature) const override
        {
            return new boss_quatermasterzigrisAI(creature);
        }
};

void AddSC_boss_quatermasterzigris()
{
    new quartermaster_zigris();
}