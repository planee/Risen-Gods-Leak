 /*
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2006-2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
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

/* ScriptData
SDName: Shadowfang_Keep
SD%Complete: 75
SDComment: npc_shadowfang_prisoner using escortAI for movement to door. Might need additional code in case being attacked. Add proper texts/say().
SDCategory: Shadowfang Keep
EndScriptData */

/* ContentData
npc_shadowfang_prisoner
EndContentData */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellScript.h"
#include "SpellAuraEffects.h"
#include "ScriptedEscortAI.h"
#include "shadowfang_keep.h"
#include "GameEventMgr.h"

/*######
## npc_shadowfang_prisoner
######*/

enum Yells
{
    SAY_FREE_AS             = 0,
    SAY_OPEN_DOOR_AS        = 1,
    SAY_POST_DOOR_AS        = 2,
    SAY_FREE_AD             = 0,
    SAY_OPEN_DOOR_AD        = 1,
    SAY_POST1_DOOR_AD       = 2,
    SAY_POST2_DOOR_AD       = 3
};

enum Spells
{
    SPELL_UNLOCK            = 6421,

    SPELL_DARK_OFFERING     = 7154
};

enum Creatures
{
    NPC_ASH                 = 3850
};

class npc_shadowfang_prisoner : public CreatureScript
{
public:
    npc_shadowfang_prisoner() : CreatureScript("npc_shadowfang_prisoner") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetInstanceAI<npc_shadowfang_prisonerAI>(creature);
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        if (action == GOSSIP_ACTION_INFO_DEF + 1)
        {
            player->CLOSE_GOSSIP_MENU();

            if (npc_escortAI* pEscortAI = CAST_AI(npc_shadowfang_prisoner::npc_shadowfang_prisonerAI, creature->AI()))
                pEscortAI->Start(false, false);
        }
        return true;
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        InstanceScript* pInstance = creature->GetInstanceScript();

        if (pInstance && pInstance->GetData(TYPE_FREE_NPC) != DONE && pInstance->GetData(TYPE_RETHILGORE) == DONE)
            player->ADD_GOSSIP_ITEM_DB(Player::GetDefaultGossipMenuForSource(creature), 0, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

        player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());

        return true;
    }

    struct npc_shadowfang_prisonerAI : public npc_escortAI
    {
        npc_shadowfang_prisonerAI(Creature* creature) : npc_escortAI(creature)
        {
            instance = creature->GetInstanceScript();
        }

        InstanceScript* instance;

        void WaypointReached(uint32 waypointId) override
        {
            switch (waypointId)
            {
                case 0:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_CHEER);
                    if (me->GetEntry() == NPC_ASH)
                        Talk(SAY_FREE_AS);
                    else
                        Talk(SAY_FREE_AD);
                    break;
                case 11:
                    if (me->GetEntry() == NPC_ASH)
                        Talk(SAY_OPEN_DOOR_AS);
                    else
                        Talk(SAY_OPEN_DOOR_AD);
                    break;
                case 12:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_USE_STANDING);
                    if (me->GetEntry() == NPC_ASH)
                    {
                        if (!IsHolidayActive(HOLIDAY_LOVE_IS_IN_THE_AIR))
                            DoCastSelf(SPELL_UNLOCK);
                    }                        
                    break;
                case 13:
                    if (me->GetEntry() == NPC_ASH)
                        Talk(SAY_POST_DOOR_AS);
                    else
                        Talk(SAY_POST1_DOOR_AD);

                    instance->SetData(TYPE_FREE_NPC, DONE);
                    break;
                case 14:
                    if (me->GetEntry() != NPC_ASH)
                        Talk(SAY_POST2_DOOR_AD);
                    SetRun();
                    break;
            }
        }

        void Reset() override { }
        void EnterCombat(Unit* /*who*/) override { }
    };

};

class npc_arugal_voidwalker : public CreatureScript
{
public:
    npc_arugal_voidwalker() : CreatureScript("npc_arugal_voidwalker") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetInstanceAI<npc_arugal_voidwalkerAI>(creature);
    }

    struct npc_arugal_voidwalkerAI : public ScriptedAI
    {
        npc_arugal_voidwalkerAI(Creature* creature) : ScriptedAI(creature)
        {
            pInstance = creature->GetInstanceScript();
        }

        InstanceScript* pInstance;

        uint32 uiDarkOffering;

        void Reset() override
        {
            uiDarkOffering = urand(200, 1000);
        }

        void UpdateAI(uint32 uiDiff) override
        {
            if (!UpdateVictim())
                return;

            if (uiDarkOffering <= uiDiff)
            {
                if (Creature* pFriend = me->FindNearestCreature(me->GetEntry(), 25.0f, true))
                {
                    if (pFriend)
                        DoCast(pFriend, SPELL_DARK_OFFERING);
                }
                else
                    DoCastSelf(SPELL_DARK_OFFERING);
                uiDarkOffering = urand(4400, 12500);
            } else uiDarkOffering -= uiDiff;

            DoMeleeAttackIfReady();
        }

        void JustDied(Unit* /*killer*/) override
        {
            pInstance->SetData(TYPE_FENRUS, pInstance->GetData(TYPE_FENRUS) + 1);
        }
    };

};

class spell_shadowfang_keep_haunting_spirits : public SpellScriptLoader
{
    public:
        spell_shadowfang_keep_haunting_spirits() : SpellScriptLoader("spell_shadowfang_keep_haunting_spirits") { }

        class spell_shadowfang_keep_haunting_spirits_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_shadowfang_keep_haunting_spirits_AuraScript);

            void CalcPeriodic(AuraEffect const* /*aurEff*/, bool& isPeriodic, int32& amplitude)
            {
                isPeriodic = true;
                amplitude = (irand(0, 60) + 30) * IN_MILLISECONDS;
            }

            void HandleDummyTick(AuraEffect const* aurEff)
            {
                GetTarget()->CastSpell((Unit*)NULL, aurEff->GetAmount(), true);
            }

            void HandleUpdatePeriodic(AuraEffect* aurEff)
            {
                aurEff->CalculatePeriodic(GetCaster());
            }

            void Register() override
            {
                DoEffectCalcPeriodic += AuraEffectCalcPeriodicFn(spell_shadowfang_keep_haunting_spirits_AuraScript::CalcPeriodic, EFFECT_0, SPELL_AURA_DUMMY);
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_shadowfang_keep_haunting_spirits_AuraScript::HandleDummyTick, EFFECT_0, SPELL_AURA_DUMMY);
                OnEffectUpdatePeriodic += AuraEffectUpdatePeriodicFn(spell_shadowfang_keep_haunting_spirits_AuraScript::HandleUpdatePeriodic, EFFECT_0, SPELL_AURA_DUMMY);
            }
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_shadowfang_keep_haunting_spirits_AuraScript();
        }
};

enum ForsakenSpells
{
    SPELL_FORSAKEN_SKILL_SWORD = 7038,
    SPELL_FORSAKEN_SKILL_SHADOW = 7053
};

class spell_shadowfang_keep_forsaken_skills : public SpellScriptLoader
{
    public:
        spell_shadowfang_keep_forsaken_skills() : SpellScriptLoader("spell_shadowfang_keep_forsaken_skills") { }

        class spell_shadowfang_keep_forsaken_skills_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_shadowfang_keep_forsaken_skills_AuraScript);

            bool Load()
            {
                _forsakenSpell = 0;
                return true;
            }

            void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
            {
                _forsakenSpell = urand(SPELL_FORSAKEN_SKILL_SWORD, SPELL_FORSAKEN_SKILL_SHADOW);
                if (_forsakenSpell == SPELL_FORSAKEN_SKILL_SHADOW - 1)
                    ++_forsakenSpell;
                GetUnitOwner()->CastSpell(GetUnitOwner(), _forsakenSpell, true);
            }

            void HandleDummyTick(AuraEffect const* aurEff)
            {
                PreventDefaultAction();
                GetUnitOwner()->RemoveAurasDueToSpell(_forsakenSpell);
                _forsakenSpell = urand(SPELL_FORSAKEN_SKILL_SWORD, SPELL_FORSAKEN_SKILL_SHADOW);
                if (_forsakenSpell == SPELL_FORSAKEN_SKILL_SHADOW - 1)
                    ++_forsakenSpell;
                GetUnitOwner()->CastSpell(GetUnitOwner(), _forsakenSpell, true);
            }

            void Register()
            {
                OnEffectApply += AuraEffectApplyFn(spell_shadowfang_keep_forsaken_skills_AuraScript::OnApply, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_shadowfang_keep_forsaken_skills_AuraScript::HandleDummyTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
            }

        private:
            uint32 _forsakenSpell;
        };

        AuraScript* GetAuraScript() const override
        {
            return new spell_shadowfang_keep_forsaken_skills_AuraScript();
        }
};

void AddSC_shadowfang_keep()
{
    new npc_shadowfang_prisoner();
    new npc_arugal_voidwalker();
    new spell_shadowfang_keep_haunting_spirits();
    new spell_shadowfang_keep_forsaken_skills();
}
