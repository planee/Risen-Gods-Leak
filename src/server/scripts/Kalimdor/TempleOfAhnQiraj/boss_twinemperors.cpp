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
SDName: Boss_Twinemperors
SD%Complete: 95
SDComment:
SDCategory: Temple of Ahn'Qiraj
EndScriptData */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "temple_of_ahnqiraj.h"
#include "WorldPacket.h"
#include "Item.h"
#include "Spell.h"

enum Spells
{
    SPELL_HEAL_BROTHER            = 7393,
    SPELL_TWIN_TELEPORT           = 800,                     // CTRA watches for this spell to start its teleport timer
    SPELL_TWIN_TELEPORT_VISUAL    = 26638,                  // visual
    SPELL_EXPLODEBUG              = 804,
    SPELL_MUTATE_BUG              = 802,
    SPELL_BERSERK                 = 26662,
    SPELL_UPPERCUT                = 26007,
    SPELL_UNBALANCING_STRIKE      = 26613,
    SPELL_SHADOWBOLT              = 26006,
    SPELL_BLIZZARD                = 26607,
    SPELL_ARCANEBURST             = 568,
};

enum Sound
{
    SOUND_VL_AGGRO                = 8657, //8657 - Aggro - To Late
    SOUND_VL_KILL                 = 8658, //8658 - Kill - You will not
    SOUND_VL_DEATH                = 8659, //8659 - Death
    SOUND_VN_DEATH                = 8660, //8660 - Death - Feel
    SOUND_VN_AGGRO                = 8661, //8661 - Aggro - Let none
    SOUND_VN_KILL                 = 8662, //8661 - Kill - your fate
};

enum Texts
{
    // yells Vek'lor
    SAY_VEKLOR_AGGRO              = 3,
    SAY_VEKLOR_SLAY               = 4,
    SAY_VEKLOR_DEATH              = 5,
    SAY_VEKLOR_SPECIAL            = 6, // unused

    // yells Vek'Nilash
    SAY_VEKNILASH_AGGRO           = 3,
    SAY_VEKNILASH_SLAY            = 4,
    SAY_VEKNILASH_DEATH           = 5,
    SAY_VEKNILASH_SPECIAL         = 6, // unused
};

enum Misc
{
    PULL_RANGE                    = 50,
    ABUSE_BUG_RANGE               = 20,
    VEKLOR_DIST                   = 20,                      // VL will not come to melee when attacking
    TELEPORTTIME                  = 30000
};



struct boss_twinemperorsAI : public ScriptedAI
{
    boss_twinemperorsAI(Creature* creature): ScriptedAI(creature)
    {
        instance = creature->GetInstanceScript();
    }

    InstanceScript* instance;

    uint32 Heal_Timer;
    uint32 Teleport_Timer;
    bool AfterTeleport;
    uint32 AfterTeleportTimer;
    bool DontYellWhenDead;
    uint32 Abuse_Bug_Timer, BugsTimer;
    bool tspellcast;
    uint32 EnrageTimer;

    virtual bool IAmVeklor() = 0;
    virtual void Reset() override = 0;
    virtual void CastSpellOnBug(Creature* target) = 0;

    void TwinReset()
    {
        Heal_Timer = 0;                                     // first heal immediately when they get close together
        Teleport_Timer = TELEPORTTIME;
        AfterTeleport = false;
        tspellcast = false;
        AfterTeleportTimer = 0;
        Abuse_Bug_Timer = urand(10000, 17000);
        BugsTimer = 2000;
        me->ClearUnitState(UNIT_STATE_STUNNED);
        DontYellWhenDead = false;
        EnrageTimer = 15*60000;
    }

    Creature* GetOtherBoss()
    {
        return ObjectAccessor::GetCreature(*me, instance->GetData64(IAmVeklor() ? DATA_TWIN_VEKLINASH : DATA_TWIN_VEKLOR));
    }

    void DamageTaken(Unit* /*done_by*/, uint32 &damage) override
    {
        Unit* pOtherBoss = GetOtherBoss();
        if (pOtherBoss)
        {
            float dPercent = ((float)damage) / ((float)me->GetMaxHealth());
            int odmg = (int)(dPercent * ((float)pOtherBoss->GetMaxHealth()));
            int ohealth = pOtherBoss->GetHealth()-odmg;
            pOtherBoss->SetHealth(ohealth > 0 ? ohealth : 0);
            if (ohealth <= 0)
            {
                pOtherBoss->setDeathState(JUST_DIED);
                pOtherBoss->SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
            }
        }
    }

    void JustDied(Unit* /*killer*/) override
    {
        Creature* pOtherBoss = GetOtherBoss();
        if (pOtherBoss)
        {
            pOtherBoss->SetHealth(0);
            pOtherBoss->setDeathState(JUST_DIED);
            pOtherBoss->SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
            ENSURE_AI(boss_twinemperorsAI, pOtherBoss->AI())->DontYellWhenDead = true;
        }
        if (!DontYellWhenDead)                             // I hope AI is not threaded
            Talk(IAmVeklor() ? SAY_VEKLOR_DEATH : SAY_VEKNILASH_DEATH);
    }

    void KilledUnit(Unit* /*victim*/) override
    {
        Talk(IAmVeklor() ? SAY_VEKLOR_SLAY : SAY_VEKNILASH_SLAY);
    }

    void EnterCombat(Unit* who) override
    {
        DoZoneInCombat();
        Creature* pOtherBoss = GetOtherBoss();
        if (pOtherBoss)
        {
            /// @todo we should activate the other boss location so he can start attackning even if nobody
            // is near I dont know how to do that
            if (!pOtherBoss->IsInCombat())
            {
                ScriptedAI* otherAI = ENSURE_AI(ScriptedAI, pOtherBoss->AI());
                Talk(IAmVeklor() ? SAY_VEKLOR_AGGRO : SAY_VEKNILASH_AGGRO);                
                otherAI->AttackStart(who);
                otherAI->DoZoneInCombat();
            }
        }
    }

    void SpellHit(Unit* caster, const SpellInfo* entry) override
    {
        if (caster == me)
            return;

        Creature* pOtherBoss = GetOtherBoss();
        if (entry->Id != SPELL_HEAL_BROTHER || !pOtherBoss)
            return;

        // add health so we keep same percentage for both brothers
        uint32 mytotal = me->GetMaxHealth(), histotal = pOtherBoss->GetMaxHealth();
        float mult = ((float)mytotal) / ((float)histotal);
        if (mult < 1)
            mult = 1.0f/mult;
        #define HEAL_BROTHER_AMOUNT 30000.0f
        uint32 largerAmount = (uint32)((HEAL_BROTHER_AMOUNT * mult) - HEAL_BROTHER_AMOUNT);

        if (mytotal > histotal)
        {
            uint32 h = me->GetHealth()+largerAmount;
            me->SetHealth(std::min(mytotal, h));
        }
        else
        {
            uint32 h = pOtherBoss->GetHealth()+largerAmount;
            pOtherBoss->SetHealth(std::min(histotal, h));
        }
    }

    void TryHealBrother(uint32 diff)
    {
        if (IAmVeklor())                                    // this spell heals caster and the other brother so let VN cast it
            return;

        if (Heal_Timer <= diff)
        {
            Unit* pOtherBoss = GetOtherBoss();
            if (pOtherBoss && pOtherBoss->IsWithinDist(me, 60))
            {
                DoCast(pOtherBoss, SPELL_HEAL_BROTHER);
                Heal_Timer = 1000;
            }
        } else Heal_Timer -= diff;
    }

    void TeleportToMyBrother()
    {
        Teleport_Timer = TELEPORTTIME;

        if (IAmVeklor())
            return;                                         // mechanics handled by veknilash so they teleport exactly at the same time and to correct coordinates

        Creature* pOtherBoss = GetOtherBoss();
        if (pOtherBoss)
        {
            //me->MonsterYell("Teleporting ...", LANG_UNIVERSAL, 0);
            Position thisPos;
            thisPos.Relocate(me);
            Position otherPos;
            otherPos.Relocate(pOtherBoss);
            pOtherBoss->SetPosition(thisPos);
            me->SetPosition(otherPos);

            SetAfterTeleport();
            ENSURE_AI(boss_twinemperorsAI, pOtherBoss->AI())->SetAfterTeleport();
        }
    }

    void SetAfterTeleport()
    {
        me->InterruptNonMeleeSpells(false);
        DoStopAttack();
        DoResetThreat();
        DoCast(me, SPELL_TWIN_TELEPORT_VISUAL);
        me->AddUnitState(UNIT_STATE_STUNNED);
        AfterTeleport = true;
        AfterTeleportTimer = 2000;
        tspellcast = false;
    }

    bool TryActivateAfterTTelep(uint32 diff)
    {
        if (AfterTeleport)
        {
            if (!tspellcast)
            {
                me->ClearUnitState(UNIT_STATE_STUNNED);
                DoCast(me, SPELL_TWIN_TELEPORT);
                me->AddUnitState(UNIT_STATE_STUNNED);
            }

            tspellcast = true;

            if (AfterTeleportTimer <= diff)
            {
                AfterTeleport = false;
                me->ClearUnitState(UNIT_STATE_STUNNED);
                if (Unit* nearu = me->SelectNearestTarget(100))
                {
                    //DoYell(nearu->GetName(), LANG_UNIVERSAL, 0);
                    AttackStart(nearu);
                    me->AddThreat(nearu, 10000);
                }
                return true;
            }
            else
            {
                AfterTeleportTimer -= diff;
                // update important timers which would otherwise get skipped
                if (EnrageTimer > diff)
                    EnrageTimer -= diff;
                else
                    EnrageTimer = 0;
                if (Teleport_Timer > diff)
                    Teleport_Timer -= diff;
                else
                    Teleport_Timer = 0;
                return false;
            }
        }
        else
        {
            return true;
        }
    }

    void MoveInLineOfSight(Unit* who) override

    {
        if (!who || me->GetVictim())
            return;

        if (me->CanCreatureAttack(who))
        {
            float attackRadius = me->GetAttackDistance(who);
            if (attackRadius < PULL_RANGE)
                attackRadius = PULL_RANGE;
            if (me->IsWithinDistInMap(who, attackRadius) && me->GetDistanceZ(who) <= /*CREATURE_Z_ATTACK_RANGE*/7 /*there are stairs*/)
            {
                //if (who->HasStealthAura())
                //    who->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);
                AttackStart(who);
            }
        }
    }

    Creature* RespawnNearbyBugsAndGetOne()
    {
        std::list<Creature*> lUnitList;
        me->GetCreatureListWithEntryInGrid(lUnitList, 15316, 150.0f);
        me->GetCreatureListWithEntryInGrid(lUnitList, 15317, 150.0f);

        if (lUnitList.empty())
            return NULL;

        Creature* nearb = NULL;

        for (std::list<Creature*>::const_iterator iter = lUnitList.begin(); iter != lUnitList.end(); ++iter)
        {
            Creature* c = *iter;
            if (c)
            {
                if (c->isDead())
                {
                    c->Respawn();
                    c->setFaction(7);
                    c->RemoveAllAuras();
                }
                if (c->IsWithinDistInMap(me, ABUSE_BUG_RANGE))
                {
                    if (!nearb || (rand32() % 4) == 0)
                        nearb = c;
                }
            }
        }
        return nearb;
    }

    void HandleBugs(uint32 diff)
    {
        if (BugsTimer < diff || Abuse_Bug_Timer <= diff)
        {
            Creature* c = RespawnNearbyBugsAndGetOne();
            if (Abuse_Bug_Timer <= diff)
            {
                if (c)
                {
                    CastSpellOnBug(c);
                    Abuse_Bug_Timer = urand(10000, 17000);
                }
                else
                {
                    Abuse_Bug_Timer = 1000;
                }
            }
            else
            {
                Abuse_Bug_Timer -= diff;
            }
            BugsTimer = 2000;
        }
        else
        {
            BugsTimer -= diff;
            Abuse_Bug_Timer -= diff;
        }
    }

    void CheckEnrage(uint32 diff)
    {
        if (EnrageTimer <= diff)
        {
            if (!me->IsNonMeleeSpellCast(true))
            {
                DoCast(me, SPELL_BERSERK);
                EnrageTimer = 60*60000;
            } else EnrageTimer = 0;
        } else EnrageTimer-=diff;
    }
};

class boss_veknilash : public CreatureScript
{
public:
    boss_veknilash() : CreatureScript("boss_veknilash") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetInstanceAI<boss_veknilashAI>(creature);
    }

    struct boss_veknilashAI : public boss_twinemperorsAI
    {
        bool IAmVeklor() {return false;}
        boss_veknilashAI(Creature* creature) : boss_twinemperorsAI(creature) { }

        uint32 UpperCut_Timer;
        uint32 UnbalancingStrike_Timer;
        uint32 Scarabs_Timer;
        int Rand;
        int RandX;
        int RandY;

        Creature* Summoned;

        void Reset() override
        {
            TwinReset();
            UpperCut_Timer = urand(14000, 29000);
            UnbalancingStrike_Timer = urand(8000, 18000);
            Scarabs_Timer = urand(7000, 14000);

                                                                //Added. Can be removed if its included in DB.
            me->ApplySpellImmune(0, IMMUNITY_DAMAGE, SPELL_SCHOOL_MASK_MAGIC, true);
        }

        void CastSpellOnBug(Creature* target) override
        {
            target->setFaction(14);
            target->AI()->AttackStart(me->getThreatManager().getHostilTarget());
            target->AddAura(SPELL_MUTATE_BUG, target);
            target->SetFullHealth();
        }

        void UpdateAI(uint32 diff) override
        {
            //Return since we have no target
            if (!UpdateVictim())
                return;

            if (!TryActivateAfterTTelep(diff))
                return;

            //UnbalancingStrike_Timer
            if (UnbalancingStrike_Timer <= diff)
            {
                DoCastVictim(SPELL_UNBALANCING_STRIKE);
                UnbalancingStrike_Timer = 8000 + rand32() % 12000;
            } else UnbalancingStrike_Timer -= diff;

            if (UpperCut_Timer <= diff)
            {
                Unit* randomMelee = SelectTarget(SELECT_TARGET_RANDOM, 0, NOMINAL_MELEE_RANGE, true);
                if (randomMelee)
                    DoCast(randomMelee, SPELL_UPPERCUT);
                UpperCut_Timer = 15000 + rand32() % 15000;
            } else UpperCut_Timer -= diff;

            HandleBugs(diff);

            //Heal brother when 60yrds close
            TryHealBrother(diff);

            //Teleporting to brother
            if (Teleport_Timer <= diff)
            {
                TeleportToMyBrother();
            } else Teleport_Timer -= diff;

            CheckEnrage(diff);

            DoMeleeAttackIfReady();
        }
    };

};

class boss_veklor : public CreatureScript
{
public:
    boss_veklor() : CreatureScript("boss_veklor") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetInstanceAI<boss_veklorAI>(creature);
    }

    struct boss_veklorAI : public boss_twinemperorsAI
    {
        bool IAmVeklor() {return true;}
        boss_veklorAI(Creature* creature) : boss_twinemperorsAI(creature) { }

        uint32 ShadowBolt_Timer;
        uint32 Blizzard_Timer;
        uint32 ArcaneBurst_Timer;
        uint32 Scorpions_Timer;
        int Rand;
        int RandX;
        int RandY;

        Creature* Summoned;

        void Reset() override
        {
            TwinReset();
            ShadowBolt_Timer = 0;
            Blizzard_Timer = urand(15000, 20000);
            ArcaneBurst_Timer = 1000;
            Scorpions_Timer = urand(7000, 14000);

            //Added. Can be removed if its included in DB.
            me->ApplySpellImmune(0, IMMUNITY_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, true);
        }

        void CastSpellOnBug(Creature* target) override
        {
            target->setFaction(14);
            target->AddAura(SPELL_EXPLODEBUG, target);
            target->SetFullHealth();
        }

        void UpdateAI(uint32 diff) override
        {
            //Return since we have no target
            if (!UpdateVictim())
                return;

            // reset arcane burst after teleport - we need to do this because
            // when VL jumps to VN's location there will be a warrior who will get only 2s to run away
            // which is almost impossible
            if (AfterTeleport)
                ArcaneBurst_Timer = 5000;
            if (!TryActivateAfterTTelep(diff))
                return;

            //ShadowBolt_Timer
            if (ShadowBolt_Timer <= diff)
            {
                if (!me->IsWithinDist(me->GetVictim(), 45.0f))
                    me->GetMotionMaster()->MoveChase(me->GetVictim(), VEKLOR_DIST, 0);
                else
                    DoCastVictim(SPELL_SHADOWBOLT);
                ShadowBolt_Timer = 2000;
            } else ShadowBolt_Timer -= diff;

            //Blizzard_Timer
            if (Blizzard_Timer <= diff)
            {
                Unit* target = NULL;
                target = SelectTarget(SELECT_TARGET_RANDOM, 0, 45, true);
                if (target)
                    DoCast(target, SPELL_BLIZZARD);
                Blizzard_Timer = 15000 + rand32() % 15000;
            } else Blizzard_Timer -= diff;

            if (ArcaneBurst_Timer <= diff)
            {
                Unit* mvic;
                if ((mvic=SelectTarget(SELECT_TARGET_NEAREST, 0, NOMINAL_MELEE_RANGE, true)) != NULL)
                {
                    DoCast(mvic, SPELL_ARCANEBURST);
                    ArcaneBurst_Timer = 5000;
                }
            } else ArcaneBurst_Timer -= diff;

            HandleBugs(diff);

            //Heal brother when 60yrds close
            TryHealBrother(diff);

            //Teleporting to brother
            if (Teleport_Timer <= diff)
            {
                TeleportToMyBrother();
            } else Teleport_Timer -= diff;

            CheckEnrage(diff);

            //VL doesn't melee
            //DoMeleeAttackIfReady();
        }

        void AttackStart(Unit* who) override
        {
            if (!who)
                return;

            if (who->isTargetableForAttack())
            {
                // VL doesn't melee
                if (me->Attack(who, false))
                {
                    me->GetMotionMaster()->MoveChase(who, VEKLOR_DIST, 0);
                    me->AddThreat(who, 0.0f);
                }
            }
        }
    };

};

enum EyeOfTheMasterMisc
{
    SAY_MASTER_EYE_INTRO = 0,
    SAY_VEKLOR_ONE       = 0,
    SAY_VEKLOR_TWO       = 1,
    SAY_VEKLOR_THREE     = 2,
    SAY_YEKNILASH_ONE    = 0,
    SAY_YEKNILASH_TWO    = 1,
    SAY_YEKNILASH_THREE  = 2,    
};

class npc_the_masters_eye : public CreatureScript
{
public:
    npc_the_masters_eye() : CreatureScript("npc_the_masters_eye") { }

    struct npc_the_masters_eyeAI : public ScriptedAI
    {
        npc_the_masters_eyeAI(Creature* creature) : ScriptedAI(creature) { }
        
        void Reset()
        {
            _EventStart = false;
            uiTimer     = 0;
            uiPhase     = 0;            
        }

        void MoveInLineOfSight(Unit* who)
        {
            if (!_EventStart && me->IsWithinDistInMap(who, 80.0f) && who->GetTypeId() == TYPEID_PLAYER)
            {
                _EventStart = true;
                uiTimer = 1*IN_MILLISECONDS;
                uiPhase = 1;
            }
            ScriptedAI::MoveInLineOfSight(who);
        }

        void UpdateAI(uint32 diff)
        {
            if (uiPhase)
            {
                if (uiTimer <= diff)
                {
                    switch (uiPhase)
                    {
                        case 1:
                            if (Creature* Veknilash = me->FindNearestCreature(NPC_TWIN_VEKLINASH, 200.0f))
                                Veknilash->SetStandState(UNIT_STAND_STATE_KNEEL);
                            if (Creature* Veklor = me->FindNearestCreature(NPC_TWIN_VEKLOR, 200.0f))
                                Veklor->SetStandState(UNIT_STAND_STATE_KNEEL);
                            uiTimer = 6 * IN_MILLISECONDS;
                            uiPhase = 2;
                            break;
                        case 2:
                            if (Creature* Veknilash = me->FindNearestCreature(NPC_TWIN_VEKLINASH, 200.0f))
                                Veknilash->SetStandState(UNIT_STAND_STATE_STAND);
                            if (Creature* Veklor = me->FindNearestCreature(NPC_TWIN_VEKLOR, 200.0f))
                                Veklor->SetStandState(UNIT_STAND_STATE_STAND);
                            uiTimer = 6 * IN_MILLISECONDS;
                            uiPhase = 3;
                            break;
                        case 3:
                            //Talk(SAY_MASTER_EYE_INTRO); -- find the correct one
                            me->SetVisible(false);
                            uiTimer = 6 * IN_MILLISECONDS;
                            uiPhase = 4;
                            break;
                        case 4:
                            if (Creature* Veklor = me->FindNearestCreature(NPC_TWIN_VEKLOR, 200.0f))
                                Veklor->AI()->Talk(SAY_VEKLOR_ONE);
                            uiTimer = 4 * IN_MILLISECONDS;
                            uiPhase = 5;
                            break;
                        case 5:
                            if (Creature* Veknilash = me->FindNearestCreature(NPC_TWIN_VEKLINASH, 200.0f))
                                Veknilash->AI()->Talk(SAY_YEKNILASH_ONE);
                            uiTimer = 4 * IN_MILLISECONDS;
                            uiPhase = 6;
                            break;
                        case 6:
                            if (Creature* Veklor = me->FindNearestCreature(NPC_TWIN_VEKLOR, 200.0f))
                                Veklor->AI()->Talk(SAY_VEKLOR_TWO);
                            uiTimer = 4 * IN_MILLISECONDS;
                            uiPhase = 7;
                            break;
                        case 7:
                            if (Creature* Veknilash = me->FindNearestCreature(NPC_TWIN_VEKLINASH, 200.0f))
                                Veknilash->AI()->Talk(SAY_YEKNILASH_TWO);
                            uiTimer = 4 * IN_MILLISECONDS;
                            uiPhase = 8;
                            break;
                        case 8:
                            if (Creature* Veklor = me->FindNearestCreature(NPC_TWIN_VEKLOR, 200.0f))
                                Veklor->AI()->Talk(SAY_VEKLOR_THREE);
                            uiTimer = 4 * IN_MILLISECONDS;
                            uiPhase = 9;
                            break;
                        case 9:
                            if (Creature* Veknilash = me->FindNearestCreature(NPC_TWIN_VEKLINASH, 200.0f))
                                Veknilash->AI()->Talk(SAY_YEKNILASH_THREE);
                            uiTimer = 4 * IN_MILLISECONDS;
                            uiPhase = 10;
                            break;
                        case 10:
                            me->DespawnOrUnsummon();
                            uiTimer = 0;
                            uiPhase = 0;
                            break;
                    }
                }
                else uiTimer -= diff;
            }
        }
        private:        
            uint32 uiTimer;
            uint32 uiPhase;
            bool _EventStart;
    };

    CreatureAI* GetAI(Creature* creature) const
    {
        return new npc_the_masters_eyeAI(creature);
    }
};

void AddSC_boss_twinemperors()
{
    new boss_veknilash();
    new boss_veklor();
    new npc_the_masters_eye();
}
