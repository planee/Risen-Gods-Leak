/*
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2012 Rising-Gods <https://www.rising-gods.de/>
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
#include "PassiveAI.h"
#include "blackwing_lair.h"
#include "Player.h"

/*
Blackwing Lair Encounter:
1 - boss_razorgore.cpp
2 - boss_vaelastrasz.cpp
3 - boss_broodlord_lashlayer.cpp
4 - boss_firemaw.cpp
5 - boss_ebonroc.cpp
6 - boss_flamegor.cpp
7 - boss_chromaggus.cpp
8 - boss_nefarian.cpp
*/

Position const SummonPosition[8] =
{
    {-7661.207520f, -1043.268188f, 407.199554f, 6.280452f},
    {-7644.145020f, -1065.628052f, 407.204956f, 0.501492f},
    {-7624.260742f, -1095.196899f, 407.205017f, 0.544694f},
    {-7608.501953f, -1116.077271f, 407.199921f, 0.816443f},
    {-7531.841797f, -1063.765381f, 407.199615f, 2.874187f},
    {-7547.319336f, -1040.971924f, 407.205078f, 3.789175f},
    {-7568.547852f, -1013.112488f, 407.204926f, 3.773467f},
    {-7584.175781f, -989.6691289f, 407.199585f, 4.527447f},
};

uint32 const Entry[5] = {12422, 12458, 12416, 12402, 12459};

class instance_blackwing_lair : public InstanceMapScript
{
public:
    instance_blackwing_lair() : InstanceMapScript("instance_blackwing_lair", 469) { }

    struct instance_blackwing_lair_InstanceMapScript : public InstanceScript
    {
        instance_blackwing_lair_InstanceMapScript(Map* map) : InstanceScript(map)
        {
            SetBossNumber(MAX_ENCOUNTER);
            SetHeaders(DataHeader);
            // Razorgore
            EggCount = 0;
            EggEvent = 0;
        }

        void Initialize() override
        {
            // Razorgore
            EggCount = 0;
            EggEvent = 0;
        }

        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_RAZORGORE:
                    RazorgoreTheUntamedGUID = creature->GetGUID();
                    break;
                case NPC_BLACKWING_DRAGON:
                case NPC_BLACKWING_TASKMASTER:
                case NPC_BLACKWING_LEGIONAIRE:
                case NPC_BLACKWING_WARLOCK:
                    if (Creature* razor = instance->GetCreature(RazorgoreTheUntamedGUID))
                        razor->AI()->JustSummoned(creature);
                    break;
                case NPC_VAELASTRAZ:
                    VaelastraszTheCorruptGUID = creature->GetGUID();
                    break;
                case NPC_BROODLORD:
                    BroodlordLashlayerGUID = creature->GetGUID();
                    break;
                case NPC_FIRENAW:
                    FiremawGUID = creature->GetGUID();
                    break;
                case NPC_EBONROC:
                    EbonrocGUID = creature->GetGUID();
                    break;
                case NPC_FLAMEGOR:
                    FlamegorGUID = creature->GetGUID();
                    break;
                case NPC_CHROMAGGUS:
                    ChromaggusGUID = creature->GetGUID();
                    break;
                case NPC_VICTOR_NEFARIUS:
                    LordVictorNefariusGUID = creature->GetGUID();
                    break;
                case NPC_NEFARIAN:
                    NefarianGUID = creature->GetGUID();
                    break;
            }
        }

        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case 177807: // Egg
                    if (GetBossState(BOSS_FIREMAW) == DONE)
                        go->SetPhaseMask(2, true);
                    else
                        EggList.push_back(go->GetGUID());
                    break;
                case 175946: // Door
                    RazorgoreDoorGUID = go->GetGUID();
                    HandleGameObject(0, GetBossState(BOSS_RAZORGORE) == DONE, go);
                    break;
                case 175185: // Door
                    VaelastraszDoorGUID = go->GetGUID();
                    HandleGameObject(0, GetBossState(BOSS_VAELASTRAZ) == DONE, go);
                    break;
                case 180424: // Door
                    BroodlordDoorGUID = go->GetGUID();
                    HandleGameObject(0, GetBossState(BOSS_BROODLORD) == DONE, go);
                    break;
                case 185483: // Door
                    ChrommagusDoorGUID = go->GetGUID();
                    HandleGameObject(0, GetBossState(BOSS_FIREMAW) == DONE && GetBossState(BOSS_EBONROC) == DONE && GetBossState(BOSS_FLAMEGOR) == DONE, go);
                    break;
                case 179117: // Door
                    HandleGameObject(0, GetBossState(BOSS_CHOMAGGUS) == DONE, go);
                    break;
                case 181125: // Door
                    NefarianDoorGUID = go->GetGUID();
                    HandleGameObject(0, GetBossState(BOSS_CHOMAGGUS) == DONE, go);
                    break;
            }
        }

        void OnGameObjectRemove(GameObject* go) override
        {
            if (go->GetEntry() == 177807) // Egg
                EggList.remove(go->GetGUID());
        }

        bool SetBossState(uint32 type, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(type, state))
                return false;

            switch (type)
            {
                case BOSS_RAZORGORE:
                    HandleGameObject(RazorgoreDoorGUID, state == DONE);
                    if (state == DONE)
                    {
                        for (std::list<uint64>::const_iterator itr = EggList.begin(); itr != EggList.end(); ++itr)
                            if (GameObject* egg = instance->GetGameObject((*itr)))
                                egg->SetPhaseMask(2, true);
                    }
                    SetData(DATA_EGG_EVENT, NOT_STARTED);
                    break;
                case BOSS_VAELASTRAZ:
                    HandleGameObject(VaelastraszDoorGUID, state == DONE);
                    break;
                case BOSS_BROODLORD:
                    HandleGameObject(BroodlordDoorGUID, state == DONE);
                    break;
                case BOSS_FIREMAW:
                case BOSS_EBONROC:
                case BOSS_FLAMEGOR:
                    HandleGameObject(ChrommagusDoorGUID, GetBossState(BOSS_FIREMAW) == DONE && GetBossState(BOSS_EBONROC) == DONE && GetBossState(BOSS_FLAMEGOR) == DONE);
                    break;
                case BOSS_CHOMAGGUS:
                    HandleGameObject(NefarianDoorGUID, state == DONE);
                    break;
                case BOSS_NEFARIAN:
                    switch (state)
                    {
                        case NOT_STARTED:
                            if (Creature* nefarian = instance->GetCreature(NefarianGUID))
                                nefarian->DespawnOrUnsummon();
                            break;
                        case FAIL:
                            _events.ScheduleEvent(EVENT_RESPAWN_NEFARIUS, 15*IN_MILLISECONDS*MINUTE);
                            SetBossState(BOSS_NEFARIAN, NOT_STARTED);
                            break;
                        default:
                            break;
                    }
                    break;
            }
            return true;
        }

        uint64 GetData64(uint32 id) const override
        {
            switch (id)
            {
                case DATA_RAZORGORE_THE_UNTAMED:  return RazorgoreTheUntamedGUID;
                case DATA_VAELASTRAZ_THE_CORRUPT: return VaelastraszTheCorruptGUID;
                case DATA_BROODLORD_LASHLAYER:    return BroodlordLashlayerGUID;
                case DATA_FIRENAW:                return FiremawGUID;
                case DATA_EBONROC:                return EbonrocGUID;
                case DATA_FLAMEGOR:               return FlamegorGUID;
                case DATA_CHROMAGGUS:             return ChromaggusGUID;
                case DATA_LORD_VICTOR_NEFARIUS:   return LordVictorNefariusGUID;
                case DATA_NEFARIAN:               return NefarianGUID;
            }

            return 0;
        }

        void SetData(uint32 type, uint32 data) override
        {
            if (type == DATA_EGG_EVENT)
            {
                switch (data)
                {
                    case IN_PROGRESS:
                        _events.ScheduleEvent(EVENT_RAZOR_SPAWN, 45000);
                        EggEvent = data;
                        EggCount = 0;
                        break;
                    case NOT_STARTED:
                        _events.CancelEvent(EVENT_RAZOR_SPAWN);
                        EggEvent = data;
                        EggCount = 0;
                        break;
                    case SPECIAL:
                        if (++EggCount == 15)
                        {
                            if (Creature* razor = instance->GetCreature(RazorgoreTheUntamedGUID))
                            {
                                SetData(DATA_EGG_EVENT, DONE);
                                razor->RemoveAurasDueToSpell(42013); // MindControl
                                DoRemoveAurasDueToSpellOnPlayers(42013);
                            }
                            _events.ScheduleEvent(EVENT_RAZOR_PHASE_TWO, IN_MILLISECONDS);
                            _events.CancelEvent(EVENT_RAZOR_SPAWN);
                        }
                        if (EggEvent == NOT_STARTED)
                            SetData(DATA_EGG_EVENT, IN_PROGRESS);
                        break;
                }
            }
        }

        void OnUnitDeath(Unit* unit) override
        {
            //! HACK, needed because of buggy CreatureAI after charm
            if (unit->GetEntry() == NPC_RAZORGORE && GetBossState(BOSS_RAZORGORE) != DONE)
                SetBossState(BOSS_RAZORGORE, DONE);
        }

        void Update(uint32 diff) override
        {
            if (_events.Empty())
                return;

            _events.Update(diff);

            while (uint32 eventId = _events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_RAZOR_SPAWN:
                        for (uint8 i = urand(2, 5); i > 0 ; --i)
                            if (Creature* summon =  instance->SummonCreature(Entry[urand(0, 5)], SummonPosition[urand(0, 8)]))
                                summon->SetInCombatWithZone();
                        _events.ScheduleEvent(EVENT_RAZOR_SPAWN, urand(12000, 17000));
                        break;
                    case EVENT_RAZOR_PHASE_TWO:
                        _events.CancelEvent(EVENT_RAZOR_SPAWN);
                        if (Creature* razor = instance->GetCreature(RazorgoreTheUntamedGUID))
                            razor->AI()->DoAction(ACTION_PHASE_TWO);
                        break;
                    case EVENT_RESPAWN_NEFARIUS:
                        if (Creature* nefarius = instance->GetCreature(LordVictorNefariusGUID))
                        {
                            nefarius->SetPhaseMask(1, true);
                            nefarius->setActive(true);
                            nefarius->Respawn();
                            nefarius->GetMotionMaster()->MoveTargetedHome();
                        }
                        break;
                }
            }
        }

    protected:
        // Razorgore
        uint8 EggCount;
        uint32 EggEvent;
        ObjectGuid RazorgoreTheUntamedGUID;
        ObjectGuid RazorgoreDoorGUID;
        GuidList EggList;

        // Vaelastrasz the Corrupt
        ObjectGuid VaelastraszTheCorruptGUID;
        ObjectGuid VaelastraszDoorGUID;

        // Broodlord Lashlayer
        ObjectGuid BroodlordLashlayerGUID;
        ObjectGuid BroodlordDoorGUID;

        // 3 Dragons
        ObjectGuid FiremawGUID;
        ObjectGuid EbonrocGUID;
        ObjectGuid FlamegorGUID;
        ObjectGuid ChrommagusDoorGUID;

        // Chormaggus
        ObjectGuid ChromaggusGUID;
        ObjectGuid NefarianDoorGUID;

        // Nefarian
        ObjectGuid LordVictorNefariusGUID;
        ObjectGuid NefarianGUID;

        // Misc
        EventMap _events;
    };

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_blackwing_lair_InstanceMapScript(map);
    }
};

class go_suppression_device  : public GameObjectScript
{
    public:
        go_suppression_device() : GameObjectScript("go_suppression_device") { }

        bool OnGossipHello(Player* player, GameObject* /*go*/)
        {
            switch (player->getClass())
            {
                case CLASS_ROGUE:
                    if (Creature* suppression = player->FindNearestCreature(NPC_SUPPRESSION_TARGET, 5.0f))
                        suppression->AI()->SetData(DATA_SUPRESSION_DEVICE, DATA_SUPRESSION_DEVICE);
                    break;
            }
            return false;
        }
};

void AddSC_instance_blackwing_lair()
{
    new instance_blackwing_lair();
    new go_suppression_device();
}
