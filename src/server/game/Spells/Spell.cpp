/*
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
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

#include "Common.h"
#include "DatabaseEnv.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Opcodes.h"
#include "Log.h"
#include "World.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Player.h"
#include "Pet.h"
#include "Unit.h"
#include "Transport.h"
#include "Spell.h"
#include "DynamicObject.h"
#include "ObjectAccessor.h"
#include "SharedDefines.h"
#include "LootMgr.h"
#include "VMapFactory.h"
#include "Battleground.h"
#include "Util.h"
#include "TemporarySummon.h"
#include "Vehicle.h"
#include "SpellAuraEffects.h"
#include "ScriptMgr.h"
#include "ConditionMgr.h"
#include "DisableMgr.h"
#include "SpellScript.h"
#include "InstanceScript.h"
#include "SpellInfo.h"
#include "SpellHistory.h"
#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "DBCStores.h"
#include "RG/Logging/LogManager.hpp"
#include "RG/Logging/SpellTimingLog.hpp"
#include "GameObjectAI.h"
#include "PathGenerator.h"

#include "SpellHooks.h"
#include "API/LoadScript.h"

extern pEffect SpellEffects[TOTAL_SPELL_EFFECTS];

SpellDestination::SpellDestination()
{
    _position.Relocate(0, 0, 0, 0);
    _transportGUID.Clear();
    _transportOffset.Relocate(0, 0, 0, 0);
}

SpellDestination::SpellDestination(float x, float y, float z, float orientation, uint32 mapId)
{
    _position.Relocate(x, y, z, orientation);
    _transportGUID.Clear();
    _position.m_mapId = mapId;
    _transportOffset.Relocate(0, 0, 0, 0);
}

SpellDestination::SpellDestination(Position const& pos)
{
    _position.Relocate(pos);
    _transportGUID.Clear();
    _transportOffset.Relocate(0, 0, 0, 0);
}

SpellDestination::SpellDestination(WorldObject const& wObj)
{
    _transportGUID = wObj.GetTransGUID();
    _transportOffset.Relocate(wObj.GetTransOffsetX(), wObj.GetTransOffsetY(), wObj.GetTransOffsetZ(), wObj.GetTransOffsetO());
    _position.Relocate(wObj);
}

void SpellDestination::Relocate(Position const& pos)
{
    if (_transportGUID)
    {
        Position offset;
        _position.GetPositionOffsetTo(pos, offset);
        _transportOffset.RelocateOffset(offset);
    }
    _position.Relocate(pos);
}

void SpellDestination::RelocateOffset(Position const& offset)
{
    if (_transportGUID)
        _transportOffset.RelocateOffset(offset);

    _position.RelocateOffset(offset);
}

SpellCastTargets::SpellCastTargets() : m_elevation(0), m_speed(0), m_strTarget()
{
    m_objectTarget = NULL;
    m_itemTarget = NULL;

    m_objectTargetGUID.Clear();
    m_itemTargetGUID.Clear();
    m_itemTargetEntry  = 0;

    m_targetMask = 0;
}

SpellCastTargets::~SpellCastTargets() { }

void SpellCastTargets::Read(ByteBuffer& data, Unit* caster)
{
    data >> m_targetMask;

    if (m_targetMask == TARGET_FLAG_NONE)
        return;

    if (m_targetMask & (TARGET_FLAG_UNIT | TARGET_FLAG_UNIT_MINIPET | TARGET_FLAG_GAMEOBJECT | TARGET_FLAG_CORPSE_ENEMY | TARGET_FLAG_CORPSE_ALLY))
        data >> m_objectTargetGUID.ReadAsPacked();

    if (m_targetMask & (TARGET_FLAG_ITEM | TARGET_FLAG_TRADE_ITEM))
        data >> m_itemTargetGUID.ReadAsPacked();

    if (m_targetMask & TARGET_FLAG_SOURCE_LOCATION)
    {
        data >> m_src._transportGUID.ReadAsPacked();
        if (m_src._transportGUID)
            data >> m_src._transportOffset.AsXYZ();
        else
            data >> m_src._position.AsXYZ();
    }
    else
    {
        m_src._transportGUID = caster->GetTransGUID();
        if (m_src._transportGUID)
            m_src._transportOffset.Relocate(caster->GetTransOffsetX(), caster->GetTransOffsetY(), caster->GetTransOffsetZ(), caster->GetTransOffsetO());
        else
            m_src._position.Relocate(caster);
    }

    if (m_targetMask & TARGET_FLAG_DEST_LOCATION)
    {
        data >> m_dst._transportGUID.ReadAsPacked();
        if (m_dst._transportGUID)
            data >> m_dst._transportOffset.AsXYZ();
        else
            data >> m_dst._position.AsXYZ();
    }
    else
    {
        m_dst._transportGUID = caster->GetTransGUID();
        if (m_dst._transportGUID)
            m_dst._transportOffset.Relocate(caster->GetTransOffsetX(), caster->GetTransOffsetY(), caster->GetTransOffsetZ(), caster->GetTransOffsetO());
        else
            m_dst._position.Relocate(caster);
    }

    if (m_targetMask & TARGET_FLAG_STRING)
        data >> m_strTarget;

    Update(caster);
}

void SpellCastTargets::Write(ByteBuffer& data)
{
    data << uint32(m_targetMask);

    if (m_targetMask & (TARGET_FLAG_UNIT | TARGET_FLAG_CORPSE_ALLY | TARGET_FLAG_GAMEOBJECT | TARGET_FLAG_CORPSE_ENEMY | TARGET_FLAG_UNIT_MINIPET))
        data << m_objectTargetGUID.WriteAsPacked();

    if (m_targetMask & (TARGET_FLAG_ITEM | TARGET_FLAG_TRADE_ITEM))
    {
        if (m_itemTarget)
            data << m_itemTarget->GetPackGUID();
        else
            data << uint8(0);
    }

    if (m_targetMask & TARGET_FLAG_SOURCE_LOCATION)
    {
        data << m_src._transportGUID.WriteAsPacked(); // relative position guid here - transport for example
        if (m_src._transportGUID)
            data << m_src._transportOffset.AsXYZ();
        else
            data << m_src._position.AsXYZ();
    }

    if (m_targetMask & TARGET_FLAG_DEST_LOCATION)
    {
        data << m_dst._transportGUID.WriteAsPacked(); // relative position guid here - transport for example
        if (m_dst._transportGUID)
            data << m_dst._transportOffset.AsXYZ();
        else
            data << m_dst._position.AsXYZ();
    }

    if (m_targetMask & TARGET_FLAG_STRING)
        data << m_strTarget;
}

ObjectGuid SpellCastTargets::GetUnitTargetGUID() const
{
    if (m_objectTargetGUID.IsUnit())
        return m_objectTargetGUID;

    return ObjectGuid::Empty;
}

Unit* SpellCastTargets::GetUnitTarget() const
{
    if (m_objectTarget)
        return m_objectTarget->ToUnit();

    return NULL;
}

void SpellCastTargets::SetUnitTarget(Unit* target)
{
    if (!target)
        return;

    m_objectTarget = target;
    m_objectTargetGUID = target->GetGUID();
    m_targetMask |= TARGET_FLAG_UNIT;
}

ObjectGuid SpellCastTargets::GetGOTargetGUID() const
{
    if (m_objectTargetGUID.IsAnyTypeGameObject())
        return m_objectTargetGUID;

    return ObjectGuid::Empty;
}

GameObject* SpellCastTargets::GetGOTarget() const
{
    if (m_objectTarget)
        return m_objectTarget->ToGameObject();

    return NULL;
}

void SpellCastTargets::SetGOTarget(GameObject* target)
{
    if (!target)
        return;

    m_objectTarget = target;
    m_objectTargetGUID = target->GetGUID();
    m_targetMask |= TARGET_FLAG_GAMEOBJECT;
}

ObjectGuid SpellCastTargets::GetCorpseTargetGUID() const
{
    if (m_objectTargetGUID.IsCorpse())
        return m_objectTargetGUID;

    return ObjectGuid::Empty;
}

Corpse* SpellCastTargets::GetCorpseTarget() const
{
    if (m_objectTarget)
        return m_objectTarget->ToCorpse();

    return NULL;
}

WorldObject* SpellCastTargets::GetObjectTarget() const
{
    return m_objectTarget;
}

ObjectGuid SpellCastTargets::GetObjectTargetGUID() const
{
    return m_objectTargetGUID;
}

void SpellCastTargets::RemoveObjectTarget()
{
    m_objectTarget = NULL;
    m_objectTargetGUID.Clear();
    m_targetMask &= ~(TARGET_FLAG_UNIT_MASK | TARGET_FLAG_CORPSE_MASK | TARGET_FLAG_GAMEOBJECT_MASK);
}

void SpellCastTargets::SetItemTarget(Item* item)
{
    if (!item)
        return;

    m_itemTarget = item;
    m_itemTargetGUID = item->GetGUID();
    m_itemTargetEntry = item->GetEntry();
    m_targetMask |= TARGET_FLAG_ITEM;
}

void SpellCastTargets::SetTradeItemTarget(Player* caster)
{
    m_itemTargetGUID.Set(uint64(TRADE_SLOT_NONTRADED));
    m_itemTargetEntry = 0;
    m_targetMask |= TARGET_FLAG_TRADE_ITEM;

    Update(caster);
}

void SpellCastTargets::UpdateTradeSlotItem()
{
    if (m_itemTarget && (m_targetMask & TARGET_FLAG_TRADE_ITEM))
    {
        m_itemTargetGUID = m_itemTarget->GetGUID();
        m_itemTargetEntry = m_itemTarget->GetEntry();
    }
}

SpellDestination const* SpellCastTargets::GetSrc() const
{
    return &m_src;
}

Position const* SpellCastTargets::GetSrcPos() const
{
    return &m_src._position;
}

void SpellCastTargets::SetSrc(float x, float y, float z)
{
    m_src = SpellDestination(x, y, z);
    m_targetMask |= TARGET_FLAG_SOURCE_LOCATION;
}

void SpellCastTargets::SetSrc(Position const& pos)
{
    m_src = SpellDestination(pos);
    m_targetMask |= TARGET_FLAG_SOURCE_LOCATION;
}

void SpellCastTargets::SetSrc(WorldObject const& wObj)
{
    m_src = SpellDestination(wObj);
    m_targetMask |= TARGET_FLAG_SOURCE_LOCATION;
}

void SpellCastTargets::ModSrc(Position const& pos)
{
    ASSERT(m_targetMask & TARGET_FLAG_SOURCE_LOCATION);
    m_src.Relocate(pos);
}

void SpellCastTargets::RemoveSrc()
{
    m_targetMask &= ~(TARGET_FLAG_SOURCE_LOCATION);
}

SpellDestination const* SpellCastTargets::GetDst() const
{
    return &m_dst;
}

WorldLocation const* SpellCastTargets::GetDstPos() const
{
    return &m_dst._position;
}

void SpellCastTargets::SetDst(float x, float y, float z, float orientation, uint32 mapId)
{
    m_dst = SpellDestination(x, y, z, orientation, mapId);
    m_targetMask |= TARGET_FLAG_DEST_LOCATION;
}

void SpellCastTargets::SetDst(Position const& pos)
{
    m_dst = SpellDestination(pos);
    m_targetMask |= TARGET_FLAG_DEST_LOCATION;
}

void SpellCastTargets::SetDst(WorldObject const& wObj)
{
    m_dst = SpellDestination(wObj);
    m_targetMask |= TARGET_FLAG_DEST_LOCATION;
}

void SpellCastTargets::SetDst(SpellDestination const& spellDest)
{
    m_dst = spellDest;
    m_targetMask |= TARGET_FLAG_DEST_LOCATION;
}

void SpellCastTargets::SetDst(SpellCastTargets const& spellTargets)
{
    m_dst = spellTargets.m_dst;
    m_targetMask |= TARGET_FLAG_DEST_LOCATION;
}

void SpellCastTargets::ModDst(Position const& pos)
{
    ASSERT(m_targetMask & TARGET_FLAG_DEST_LOCATION);
    m_dst.Relocate(pos);
}

void SpellCastTargets::ModDst(SpellDestination const& spellDest)
{
    ASSERT(m_targetMask & TARGET_FLAG_DEST_LOCATION);
    m_dst = spellDest;
}

void SpellCastTargets::RemoveDst()
{
    m_targetMask &= ~(TARGET_FLAG_DEST_LOCATION);
}

void SpellCastTargets::Update(Unit* caster)
{
    m_objectTarget = m_objectTargetGUID ? ((m_objectTargetGUID == caster->GetGUID()) ? caster : ObjectAccessor::GetWorldObject(*caster, m_objectTargetGUID)) : NULL;

    m_itemTarget = NULL;
    if (caster->GetTypeId() == TYPEID_PLAYER)
    {
        Player* player = caster->ToPlayer();
        if (m_targetMask & TARGET_FLAG_ITEM)
            m_itemTarget = player->GetItemByGuid(m_itemTargetGUID);
        else if (m_targetMask & TARGET_FLAG_TRADE_ITEM)
            if (m_itemTargetGUID.GetRawValue() == TRADE_SLOT_NONTRADED) // here it is not guid but slot. Also prevents hacking slots
                if (TradeData* pTrade = player->GetTradeData())
                    m_itemTarget = pTrade->GetTraderData()->GetItem(TRADE_SLOT_NONTRADED);

        if (m_itemTarget)
            m_itemTargetEntry = m_itemTarget->GetEntry();
    }

    // update positions by transport move
    if (HasSrc() && m_src._transportGUID)
    {
        if (WorldObject* transport = ObjectAccessor::GetWorldObject(*caster, m_src._transportGUID))
        {
            m_src._position.Relocate(transport);
            m_src._position.RelocateOffset(m_src._transportOffset);
        }
    }

    if (HasDst() && m_dst._transportGUID)
    {
        if (WorldObject* transport = ObjectAccessor::GetWorldObject(*caster, m_dst._transportGUID))
        {
            m_dst._position.Relocate(transport);
            m_dst._position.RelocateOffset(m_dst._transportOffset);
        }
    }
}

void SpellCastTargets::OutDebug() const
{
    if (!m_targetMask)
        TC_LOG_DEBUG("spells", "No targets");

    TC_LOG_DEBUG("spells", "target mask: %u", m_targetMask);
    if (m_targetMask & (TARGET_FLAG_UNIT_MASK | TARGET_FLAG_CORPSE_MASK | TARGET_FLAG_GAMEOBJECT_MASK))
        TC_LOG_DEBUG("spells", "Object target: " UI64FMTD, m_objectTargetGUID.GetRawValue());
    if (m_targetMask & TARGET_FLAG_ITEM)
        TC_LOG_DEBUG("spells", "Item target: " UI64FMTD, m_itemTargetGUID.GetRawValue());
    if (m_targetMask & TARGET_FLAG_TRADE_ITEM)
        TC_LOG_DEBUG("spells", "Trade item target: " UI64FMTD, m_itemTargetGUID.GetRawValue());
    if (m_targetMask & TARGET_FLAG_SOURCE_LOCATION)
        TC_LOG_DEBUG("spells", "Source location: transport guid:" UI64FMTD " trans offset: %s position: %s", m_src._transportGUID.GetRawValue(), m_src._transportOffset.ToString().c_str(), m_src._position.ToString().c_str());
    if (m_targetMask & TARGET_FLAG_DEST_LOCATION)
        TC_LOG_DEBUG("spells", "Destination location: transport guid:" UI64FMTD " trans offset: %s position: %s", m_dst._transportGUID.GetRawValue(), m_dst._transportOffset.ToString().c_str(), m_dst._position.ToString().c_str());
    if (m_targetMask & TARGET_FLAG_STRING)
        TC_LOG_DEBUG("spells", "String: %s", m_strTarget.c_str());
    TC_LOG_DEBUG("spells", "speed: %f", m_speed);
    TC_LOG_DEBUG("spells", "elevation: %f", m_elevation);
}

SpellValue::SpellValue(SpellInfo const* proto)
{
    for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        EffectBasePoints[i] = proto->Effects[i].BasePoints;
    MaxAffectedTargets = proto->MaxAffectedTargets;
    RadiusMod = 1.0f;
    AuraStackAmount = 1;
}

Spell::Spell(Unit* caster, SpellInfo const* info, TriggerCastFlags triggerFlags, ObjectGuid originalCasterGUID, bool skipCheck) :
m_spellInfo(sSpellMgr->GetSpellForDifficultyFromSpell(info, caster)), visualSpellId(info->Id),
m_caster((info->HasAttribute(SPELL_ATTR6_CAST_BY_CHARMER) && caster->GetCharmerOrOwner()) ? caster->GetCharmerOrOwner() : caster)
, m_spellValue(new SpellValue(m_spellInfo)), delayedDuelCheck(false)
{
    m_customError = SPELL_CUSTOM_ERROR_NONE;
    m_skipCheck = skipCheck;
    m_selfContainer = NULL;
    m_referencedFromCurrentSpell = false;
    m_executedCurrently = false;
    m_needComboPoints = m_spellInfo->NeedsComboPoints();
    m_comboPointGain = 0;
    m_delayStart = 0;
    m_delayAtDamageCount = 0;

    m_applyMultiplierMask = 0;
    m_auraScaleMask = 0;
    memset(m_damageMultipliers, 0, sizeof(m_damageMultipliers));

    // Get data for type of attack
    switch (m_spellInfo->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_MELEE:
            if (m_spellInfo->HasAttribute(SPELL_ATTR3_REQ_OFFHAND))
                m_attackType = OFF_ATTACK;
            else
                m_attackType = BASE_ATTACK;
            break;
        case SPELL_DAMAGE_CLASS_RANGED:
            m_attackType = m_spellInfo->IsRangedWeaponSpell() ? RANGED_ATTACK : BASE_ATTACK;
            break;
        default:
                                                            // Wands
            if (m_spellInfo->HasAttribute(SPELL_ATTR2_AUTOREPEAT_FLAG))
                m_attackType = RANGED_ATTACK;
            else
                m_attackType = BASE_ATTACK;
            break;
    }

    m_spellSchoolMask = info->GetSchoolMask();           // Can be override for some spell (wand shoot for example)

    if (m_attackType == RANGED_ATTACK)
        // wand case
        if ((m_caster->getClassMask() & CLASSMASK_WAND_USERS) != 0 && m_caster->GetTypeId() == TYPEID_PLAYER)
            if (Item* pItem = m_caster->ToPlayer()->GetWeaponForAttack(RANGED_ATTACK))
                m_spellSchoolMask = SpellSchoolMask(1 << pItem->GetTemplate()->Damage[0].DamageType);

    if (originalCasterGUID)
        m_originalCasterGUID = originalCasterGUID;
    else
        m_originalCasterGUID = m_caster->GetGUID();

    if (m_originalCasterGUID == m_caster->GetGUID())
        m_originalCaster = m_caster;
    else
    {
        m_originalCaster = ObjectAccessor::GetUnit(*m_caster, m_originalCasterGUID);
        if (m_originalCaster && !m_originalCaster->IsInWorld())
            m_originalCaster = NULL;
    }

    m_spellState = SPELL_STATE_NULL;
    _triggeredCastFlags = triggerFlags;
    if (info->HasAttribute(SPELL_ATTR4_CAN_CAST_WHILE_CASTING))
        _triggeredCastFlags = TriggerCastFlags(uint32(_triggeredCastFlags) | TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_CAST_DIRECTLY);

    m_CastItem = NULL;
    m_castItemGUID.Clear();
    m_castItemEntry = 0;

    unitTarget = NULL;
    itemTarget = NULL;
    gameObjTarget = NULL;
    destTarget = NULL;
    damage = 0;
    effectHandleMode = SPELL_EFFECT_HANDLE_LAUNCH;
    m_diminishLevel = DIMINISHING_LEVEL_1;
    m_diminishGroup = DIMINISHING_NONE;
    m_damage = 0;
    m_healing = 0;
    m_procAttacker = 0;
    m_procVictim = 0;
    m_procEx = 0;
    focusObject = NULL;
    m_cast_count = 0;
    m_glyphIndex = 0;
    m_preCastSpell = 0;
    m_triggeredByAuraSpell  = NULL;
    m_spellAura = NULL;
    m_softRefresh = false;

    //Auto Shot & Shoot (wand)
    m_autoRepeat = m_spellInfo->IsAutoRepeatRangedSpell();

    m_runesState = 0;
    m_powerCost = 0;                                        // setup to correct value in Spell::prepare, must not be used before.
    m_casttime = 0;                                         // setup to correct value in Spell::prepare, must not be used before.
    m_timer = 0;                                            // will set to castime in prepare
    m_channeledDuration = 0;                                // will be setup in Spell::handle_immediate
    m_immediateHandled = false;

    m_channelTargetEffectMask = 0;

    // Determine if spell can be reflected back to the caster
    // Patch 1.2 notes: Spell Reflection no longer reflects abilities
    // some exeptions for spells which can be reflected
    if ((m_spellInfo->SpellFamilyName == SPELLFAMILY_SHAMAN && m_spellInfo->SpellFamilyFlags[1] == 0x2000)
        || (m_spellInfo->SpellFamilyName == SPELLFAMILY_MAGE && m_spellInfo->SpellFamilyFlags[0] == 0x200000))
        m_canReflect = true;
    else
    {
        m_canReflect = m_spellInfo->HasAttribute(SPELL_ATTR0_CU_FORCE_REFLECT) || (m_spellInfo->DmgClass == SPELL_DAMAGE_CLASS_MAGIC && !m_spellInfo->HasAttribute(SPELL_ATTR0_ABILITY)
            && !m_spellInfo->HasAttribute(SPELL_ATTR1_CANT_BE_REFLECTED) && !m_spellInfo->HasAttribute(SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY)
            && !m_spellInfo->IsPassive() && (!m_spellInfo->IsPositive() || m_spellInfo->HasEffect(SPELL_EFFECT_DISPEL)) && !m_spellInfo->IsAffectingArea());
    }
    CleanupTargetList();
    memset(m_effectExecuteData, 0, MAX_SPELL_EFFECTS * sizeof(ByteBuffer*));

    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        m_destTargets[i] = SpellDestination(*m_caster);

    m_castResult = SPELL_CAST_OK;

    InitScript(m_spellInfo->ScriptName);
}

Spell::~Spell()
{
    unloadScript(*this);

    // unload scripts
    while (!m_loadedScripts.empty())
    {
        std::list<SpellScript*>::iterator itr = m_loadedScripts.begin();
        (*itr)->_Unload();
        delete (*itr);
        m_loadedScripts.erase(itr);
    }

    if (m_referencedFromCurrentSpell && m_selfContainer && *m_selfContainer == this)
    {
        // Clean the reference to avoid later crash.
        // If this error is repeating, we may have to add an ASSERT to better track down how we get into this case.
        TC_LOG_ERROR("spells", "SPELL: deleting spell for spell ID %u. However, spell still referenced.", m_spellInfo->Id);
        *m_selfContainer = NULL;
    }

    // Keep spellUsingModsFromThisAura list in all auras currently used by this spell valid
    while (!m_appliedMods.empty())
    {
        std::set<Aura*>::iterator itr = m_appliedMods.begin();
        RemoveUsedModAura((*itr));
    }

    if (m_caster && m_caster->GetTypeId() == TYPEID_PLAYER)
        ASSERT(m_caster->ToPlayer()->m_spellModTakingSpell != this);

    delete m_spellValue;

    CheckEffectExecuteData();
}

void Spell::InitExplicitTargets(SpellCastTargets const& targets)
{
    m_targets = targets;
    // this function tries to correct spell explicit targets for spell
    // client doesn't send explicit targets correctly sometimes - we need to fix such spells serverside
    // this also makes sure that we correctly send explicit targets to client (removes redundant data)
    uint32 neededTargets = m_spellInfo->GetExplicitTargetMask();

    if (WorldObject* target = m_targets.GetObjectTarget())
    {
        // check if object target is valid with needed target flags
        // for unit case allow corpse target mask because player with not released corpse is a unit target
        if ((target->ToUnit() && !(neededTargets & (TARGET_FLAG_UNIT_MASK | TARGET_FLAG_CORPSE_MASK)))
            || (target->ToGameObject() && !(neededTargets & TARGET_FLAG_GAMEOBJECT_MASK))
            || (target->ToCorpse() && !(neededTargets & TARGET_FLAG_CORPSE_MASK)))
            m_targets.RemoveObjectTarget();
    }
    else
    {
        // try to select correct unit target if not provided by client or by serverside cast
        if (neededTargets & (TARGET_FLAG_UNIT_MASK))
        {
            Unit* unit = NULL;
            // try to use player selection as a target
            if (Player* playerCaster = m_caster->ToPlayer())
            {
                // selection has to be found and to be valid target for the spell
                if (Unit* selectedUnit = ObjectAccessor::GetUnit(*m_caster, playerCaster->GetTarget()))
                    if (m_spellInfo->CheckExplicitTarget(m_caster, selectedUnit) == SPELL_CAST_OK)
                        unit = selectedUnit;
            }
            // try to use attacked unit as a target
            else if ((m_caster->GetTypeId() == TYPEID_UNIT) && neededTargets & (TARGET_FLAG_UNIT_ENEMY | TARGET_FLAG_UNIT))
            {
                unit = m_caster->GetVictim();

                if (!unit && neededTargets & TARGET_FLAG_UNIT)
                    unit = m_caster;
            }

            // didn't find anything - let's use self as target
            if (!unit && neededTargets & (TARGET_FLAG_UNIT_RAID | TARGET_FLAG_UNIT_PARTY | TARGET_FLAG_UNIT_ALLY))
                unit = m_caster;

            m_targets.SetUnitTarget(unit);
        }
    }

    // check if spell needs dst target
    if (neededTargets & TARGET_FLAG_DEST_LOCATION)
    {
        // and target isn't set
        if (!m_targets.HasDst())
        {
            // try to use unit target if provided
            if (WorldObject* target = targets.GetObjectTarget())
                m_targets.SetDst(*target);
            // or use self if not available
            else
                m_targets.SetDst(*m_caster);
        }
    }
    else
        m_targets.RemoveDst();

    if (neededTargets & TARGET_FLAG_SOURCE_LOCATION)
    {
        if (!targets.HasSrc())
            m_targets.SetSrc(*m_caster);
    }
    else
        m_targets.RemoveSrc();
}

void Spell::SelectExplicitTargets()
{
    // here go all explicit target changes made to explicit targets after spell prepare phase is finished
    if (Unit* target = m_targets.GetUnitTarget())
    {
        switch (m_spellInfo->Id)
        {
            case 44572:    // Deep Freeze
            case 14309:    // Traps
            case 55041:    // Freezing Trap
            case 60210:    // Freezing Arrow
            case 17962:    // Conflagrate
            case 527:      // Dispel Magic
            case 988:      // Dispel Magic
                if (Aura* aura = target->GetAura(8178)) // Grounding Totem Effect
                    if (Unit* redirect_unit = aura->GetOwner()->ToUnit())
                    {
                        // Redirect target
                        m_targets.SetUnitTarget(redirect_unit);
                        // Redurected owner should die
                        redirect_unit->setDeathState(JUST_DIED);
                    }
                break;
        }
        // check for explicit target redirection, for Grounding Totem for example
        if (m_spellInfo->GetExplicitTargetMask() & TARGET_FLAG_UNIT_ENEMY
            || (m_spellInfo->GetExplicitTargetMask() & TARGET_FLAG_UNIT && !m_caster->IsFriendlyTo(target)))
        {
            Unit* redirect;
            switch (m_spellInfo->DmgClass)
            {
                case SPELL_DAMAGE_CLASS_MAGIC:
                    redirect = m_caster->GetMagicHitRedirectTarget(target, m_spellInfo);
                    break;
                case SPELL_DAMAGE_CLASS_MELEE:
                case SPELL_DAMAGE_CLASS_RANGED:
                    redirect = m_caster->GetMeleeHitRedirectTarget(target, m_spellInfo);
                    break;
                default:
                    redirect = NULL;
                    break;
            }
            if (redirect && (redirect != target))
                m_targets.SetUnitTarget(redirect);
        }
    }
}

void Spell::SelectSpellTargets()
{
    // select targets for cast phase
    SelectExplicitTargets();

    uint32 processedAreaEffectsMask = 0;
    for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        // not call for empty effect.
        // Also some spells use not used effect targets for store targets for dummy effect in triggered spells
        if (!m_spellInfo->Effects[i].IsEffect())
            continue;

        // set expected type of implicit targets to be sent to client
        uint32 implicitTargetMask = GetTargetFlagMask(m_spellInfo->Effects[i].TargetA.GetObjectType()) | GetTargetFlagMask(m_spellInfo->Effects[i].TargetB.GetObjectType());
        if (implicitTargetMask & TARGET_FLAG_UNIT)
            m_targets.SetTargetFlag(TARGET_FLAG_UNIT);
        if (implicitTargetMask & (TARGET_FLAG_GAMEOBJECT | TARGET_FLAG_GAMEOBJECT_ITEM))
            m_targets.SetTargetFlag(TARGET_FLAG_GAMEOBJECT);

        SelectEffectImplicitTargets(SpellEffIndex(i), m_spellInfo->Effects[i].TargetA, processedAreaEffectsMask);
        SelectEffectImplicitTargets(SpellEffIndex(i), m_spellInfo->Effects[i].TargetB, processedAreaEffectsMask);

        // Select targets of effect based on effect type
        // those are used when no valid target could be added for spell effect based on spell target type
        // some spell effects use explicit target as a default target added to target map (like SPELL_EFFECT_LEARN_SPELL)
        // some spell effects add target to target map only when target type specified (like SPELL_EFFECT_WEAPON)
        // some spell effects don't add anything to target map (confirmed with sniffs) (like SPELL_EFFECT_DESTROY_ALL_TOTEMS)
        SelectEffectTypeImplicitTargets(i);

        if (m_targets.HasDst())
            AddDestTarget(*m_targets.GetDst(), i);

        if (m_spellInfo->IsChanneled())
        {
            uint8 mask = (1 << i);
            for (std::list<TargetInfo>::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
            {
                if (ihit->effectMask & mask)
                {
                    m_channelTargetEffectMask |= mask;
                    break;
                }
            }
        }
        else if (m_auraScaleMask)
        {
            bool checkLvl = !m_UniqueTargetInfo.empty();
            for (std::list<TargetInfo>::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end();)
            {
                // remove targets which did not pass min level check
                if (m_auraScaleMask && ihit->effectMask == m_auraScaleMask)
                {
                    // Do not check for selfcast
                    if (!ihit->scaleAura && ihit->targetGUID != m_caster->GetGUID())
                    {
                         m_UniqueTargetInfo.erase(ihit++);
                         continue;
                    }
                }
                ++ihit;
            }
            if (checkLvl && m_UniqueTargetInfo.empty())
            {
                SendCastResult(SPELL_FAILED_LOWLEVEL);
                finish(false);
            }
        }
    }

    // Some spells should fail if they have no target
    if (m_UniqueTargetInfo.empty())
    {
        switch (m_spellInfo->Id)
        {
            case 51640: // Taunt Flag Targeting
                SendCastResult(SPELL_FAILED_NO_VALID_TARGETS);
                finish(false);
                break;
            default:
                break;
        }
    }

    if (m_targets.HasDst())
    {
        if (m_targets.HasTraj())
        {
            float speed = m_targets.GetSpeedXY();
            if (speed > 0.0f)
                m_delayMoment = (uint64)floor(m_targets.GetDist2d() / speed * 1000.0f);
        }
        else if (m_spellInfo->Speed > 0.0f)
        {
            float dist = m_caster->GetDistance(*m_targets.GetDstPos());
            m_delayMoment = (uint64) std::floor(dist / m_spellInfo->Speed * 1000.0f);
        }
    }
}

void Spell::SelectEffectImplicitTargets(SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType, uint32& processedEffectMask)
{
    if (!targetType.GetTarget())
        return;

    uint32 effectMask = 1 << effIndex;
    // set the same target list for all effects
    // some spells appear to need this, however this requires more research
    switch (targetType.GetSelectionCategory())
    {
        case TARGET_SELECT_CATEGORY_NEARBY:
        case TARGET_SELECT_CATEGORY_CONE:
        case TARGET_SELECT_CATEGORY_AREA:
            // targets for effect already selected
            if (effectMask & processedEffectMask)
                return;
            // choose which targets we can select at once
            for (uint32 j = effIndex + 1; j < MAX_SPELL_EFFECTS; ++j)
            {
                SpellEffectInfo const* effects = GetSpellInfo()->Effects;
                if (effects[j].IsEffect() &&
                    effects[effIndex].TargetA.GetTarget() == effects[j].TargetA.GetTarget() &&
                    effects[effIndex].TargetB.GetTarget() == effects[j].TargetB.GetTarget() &&
                    effects[effIndex].ImplicitTargetConditions == effects[j].ImplicitTargetConditions &&
                    effects[effIndex].CalcRadius(m_caster) == effects[j].CalcRadius(m_caster) &&
                    CheckScriptEffectImplicitTargets(effIndex, j))
                {
                    effectMask |= 1 << j;
                }
            }
            processedEffectMask |= effectMask;
            break;
        default:
            break;
    }

    switch (targetType.GetSelectionCategory())
    {
        case TARGET_SELECT_CATEGORY_CHANNEL:
            SelectImplicitChannelTargets(effIndex, targetType);
            break;
        case TARGET_SELECT_CATEGORY_NEARBY:
            SelectImplicitNearbyTargets(effIndex, targetType, effectMask);
            break;
        case TARGET_SELECT_CATEGORY_CONE:
            SelectImplicitConeTargets(effIndex, targetType, effectMask);
            break;
        case TARGET_SELECT_CATEGORY_AREA:
            SelectImplicitAreaTargets(effIndex, targetType, effectMask);
            break;
        case TARGET_SELECT_CATEGORY_DEFAULT:
            switch (targetType.GetObjectType())
            {
                case TARGET_OBJECT_TYPE_SRC:
                    switch (targetType.GetReferenceType())
                    {
                        case TARGET_REFERENCE_TYPE_CASTER:
                            m_targets.SetSrc(*m_caster);
                            break;
                        default:
                            ASSERT(false && "Spell::SelectEffectImplicitTargets: received not implemented select target reference type for TARGET_TYPE_OBJECT_SRC");
                            break;
                    }
                    break;
                case TARGET_OBJECT_TYPE_DEST:
                     switch (targetType.GetReferenceType())
                     {
                         case TARGET_REFERENCE_TYPE_CASTER:
                             SelectImplicitCasterDestTargets(effIndex, targetType);
                             break;
                         case TARGET_REFERENCE_TYPE_TARGET:
                             SelectImplicitTargetDestTargets(effIndex, targetType);
                             break;
                         case TARGET_REFERENCE_TYPE_DEST:
                             SelectImplicitDestDestTargets(effIndex, targetType);
                             break;
                         default:
                             ASSERT(false && "Spell::SelectEffectImplicitTargets: received not implemented select target reference type for TARGET_TYPE_OBJECT_DEST");
                             break;
                     }
                     break;
                default:
                    switch (targetType.GetReferenceType())
                    {
                        case TARGET_REFERENCE_TYPE_CASTER:
                            SelectImplicitCasterObjectTargets(effIndex, targetType);
                            break;
                        case TARGET_REFERENCE_TYPE_TARGET:
                            SelectImplicitTargetObjectTargets(effIndex, targetType);
                            break;
                        default:
                            ASSERT(false && "Spell::SelectEffectImplicitTargets: received not implemented select target reference type for TARGET_TYPE_OBJECT");
                            break;
                    }
                    break;
            }
            break;
        case TARGET_SELECT_CATEGORY_NYI:
            TC_LOG_DEBUG("spells", "SPELL: target type %u, found in spellID %u, effect %u is not implemented yet!", m_spellInfo->Id, effIndex, targetType.GetTarget());
            break;
        default:
            ASSERT(false && "Spell::SelectEffectImplicitTargets: received not implemented select target category");
            break;
    }
}

void Spell::SelectImplicitChannelTargets(SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType)
{
    if (targetType.GetReferenceType() != TARGET_REFERENCE_TYPE_CASTER)
    {
        ASSERT(false && "Spell::SelectImplicitChannelTargets: received not implemented target reference type");
        return;
    }

    Spell* channeledSpell = m_originalCaster->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (!channeledSpell && (targetType.GetTarget() != TARGET_UNIT_CHANNEL_TARGET || !m_caster->lastChannelTarget))
    {
        TC_LOG_DEBUG("spells", "Spell::SelectImplicitChannelTargets: cannot find channel spell for spell ID %u, effect %u", m_spellInfo->Id, effIndex);
        return;
    }
    switch (targetType.GetTarget())
    {
        case TARGET_UNIT_CHANNEL_TARGET:
        {
            ObjectGuid targetGuid = m_originalCaster->GetGuidValue(UNIT_FIELD_CHANNEL_OBJECT);
            if (!targetGuid)
                targetGuid = m_caster->lastChannelTarget;
            WorldObject* target = ObjectAccessor::GetUnit(*m_caster, targetGuid);
            CallScriptObjectTargetSelectHandlers(target, effIndex, targetType);
            // unit target may be no longer avalible - teleported out of map for example
            if (target && target->ToUnit())
                AddUnitTarget(target->ToUnit(), 1 << effIndex);
            else
                TC_LOG_DEBUG("spells", "SPELL: cannot find channel spell target for spell ID %u, effect %u", m_spellInfo->Id, effIndex);
            break;
        }
        case TARGET_DEST_CHANNEL_TARGET:
            if (channeledSpell->m_targets.HasDst())
                m_targets.SetDst(channeledSpell->m_targets);
            else if (WorldObject* target = ObjectAccessor::GetWorldObject(*m_caster, m_originalCaster->GetChannelObjectGuid()))
            {
                CallScriptObjectTargetSelectHandlers(target, effIndex, targetType);
                if (target)
                {
                    SpellDestination dest(*target);
                    CallScriptDestinationTargetSelectHandlers(dest, effIndex, targetType);
                    m_targets.SetDst(dest);
                }
            }
            else
                TC_LOG_DEBUG("spells", "SPELL: cannot find channel spell destination for spell ID %u, effect %u", m_spellInfo->Id, effIndex);
            break;
        case TARGET_DEST_CHANNEL_CASTER:
        {
            SpellDestination dest(*channeledSpell->GetCaster());
            CallScriptDestinationTargetSelectHandlers(dest, effIndex, targetType);
            m_targets.SetDst(dest);
            break;
        }
        default:
            ASSERT(false && "Spell::SelectImplicitChannelTargets: received not implemented target type");
            break;
    }
}

void Spell::SelectImplicitNearbyTargets(SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType, uint32 effMask)
{
    if (targetType.GetReferenceType() != TARGET_REFERENCE_TYPE_CASTER)
    {
        ASSERT(false && "Spell::SelectImplicitNearbyTargets: received not implemented target reference type");
        return;
    }

    float range = 0.0f;
    switch (targetType.GetCheckType())
    {
        case TARGET_CHECK_ENEMY:
            range = m_spellInfo->GetMaxRange(false, m_caster, this);
            break;
        case TARGET_CHECK_ALLY:
        case TARGET_CHECK_PARTY:
        case TARGET_CHECK_RAID:
        case TARGET_CHECK_RAID_CLASS:
            range = m_spellInfo->GetMaxRange(true, m_caster, this);
            break;
        case TARGET_CHECK_ENTRY:
        case TARGET_CHECK_DEFAULT:
            range = m_spellInfo->GetMaxRange(m_spellInfo->IsPositive(), m_caster, this);
            break;
        default:
            ASSERT(false && "Spell::SelectImplicitNearbyTargets: received not implemented selection check type");
            break;
    }

    ConditionContainer* condList = m_spellInfo->Effects[effIndex].ImplicitTargetConditions;

    // handle emergency case - try to use other provided targets if no conditions provided
    if (targetType.GetCheckType() == TARGET_CHECK_ENTRY && (!condList || condList->empty()))
    {
        TC_LOG_DEBUG("spells", "Spell::SelectImplicitNearbyTargets: no conditions entry for target with TARGET_CHECK_ENTRY of spell ID %u, effect %u - selecting default targets", m_spellInfo->Id, effIndex);
        switch (targetType.GetObjectType())
        {
            case TARGET_OBJECT_TYPE_GOBJ:
                if (m_spellInfo->RequiresSpellFocus)
                {
                    if (focusObject)
                        AddGOTarget(focusObject, effMask);
                    return;
                }
                break;
            case TARGET_OBJECT_TYPE_DEST:
                if (m_spellInfo->RequiresSpellFocus)
                {
                    if (focusObject)
                    {
                        SpellDestination dest(*focusObject);
                        CallScriptDestinationTargetSelectHandlers(dest, effIndex, targetType);
                        m_targets.SetDst(dest);
                    }
                    return;
                }
                break;
            default:
                break;
        }
    }

    WorldObject* target = SearchNearbyTarget(range, targetType.GetObjectType(), targetType.GetCheckType(), condList);
    if (!target)
    {
        TC_LOG_DEBUG("spells", "Spell::SelectImplicitNearbyTargets: cannot find nearby target for spell ID %u, effect %u", m_spellInfo->Id, effIndex);
        return;
    }

    CallScriptObjectTargetSelectHandlers(target, effIndex, targetType);
    if (!target)
    {
        TC_LOG_DEBUG("spells", "Spell::SelectImplicitNearbyTargets: OnObjectTargetSelect script hook for spell Id %u set NULL target, effect %u", m_spellInfo->Id, effIndex);
        return;
    }

    switch (targetType.GetObjectType())
    {
        case TARGET_OBJECT_TYPE_UNIT:
            if (Unit* unitTarget = target->ToUnit())
                AddUnitTarget(unitTarget, effMask, true, false);
            else
            {
                TC_LOG_DEBUG("spells", "Spell::SelectImplicitNearbyTargets: OnObjectTargetSelect script hook for spell Id %u set object of wrong type, expected unit, got %s, effect %u", m_spellInfo->Id, target->GetGUID().GetTypeName().data(), effMask);
                return;
            }
            break;
        case TARGET_OBJECT_TYPE_GOBJ:
            if (GameObject* gobjTarget = target->ToGameObject())
                AddGOTarget(gobjTarget, effMask);
            else
            {
                TC_LOG_DEBUG("spells", "Spell::SelectImplicitNearbyTargets: OnObjectTargetSelect script hook for spell Id %u set object of wrong type, expected gameobject, got %s, effect %u", m_spellInfo->Id, target->GetGUID().GetTypeName().data(), effMask);
                return;
            }
            break;
        case TARGET_OBJECT_TYPE_DEST:
        {
            SpellDestination dest(*target);
            CallScriptDestinationTargetSelectHandlers(dest, effIndex, targetType);
            m_targets.SetDst(dest);
            break;
        }
        default:
            ASSERT(false && "Spell::SelectImplicitNearbyTargets: received not implemented target object type");
            break;
    }

    SelectImplicitChainTargets(effIndex, targetType, target, effMask);
}

void Spell::SelectImplicitConeTargets(SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType, uint32 effMask)
{
    if (targetType.GetReferenceType() != TARGET_REFERENCE_TYPE_CASTER)
    {
        ASSERT(false && "Spell::SelectImplicitConeTargets: received not implemented target reference type");
        return;
    }
    std::list<WorldObject*> targets;
    SpellTargetObjectTypes objectType = targetType.GetObjectType();
    SpellTargetCheckTypes selectionType = targetType.GetCheckType();
    ConditionContainer* condList = m_spellInfo->Effects[effIndex].ImplicitTargetConditions;

    const auto targetA = m_spellInfo->Effects[effIndex].TargetA;
    const auto targetB = m_spellInfo->Effects[effIndex].TargetB;

    float coneAngle;
    // Target effect 24 should have ~54 degree angle
    if (targetA.GetTarget() == TARGET_UNIT_CONE_ENEMY_24 || targetB.GetTarget() == TARGET_UNIT_CONE_ENEMY_24)
        coneAngle = float(M_PI) * (54.0f / 180.0f);
    else
        coneAngle = float(M_PI) / 2;

    float radius = m_spellInfo->Effects[effIndex].CalcRadius(m_caster) * m_spellValue->RadiusMod;

    if (uint32 containerTypeMask = GetSearcherTypeMask(objectType, condList))
    {
        Trinity::WorldObjectSpellConeTargetCheck check(coneAngle, radius, m_caster, m_spellInfo, selectionType, condList);
        Trinity::MapTypeMaskCheck mask(containerTypeMask);
        auto searcher = Trinity::createBaseObjectSearcher<WorldObject, Trinity::ContainerAction>(m_caster, targets, check, mask);

        SearchTargets(searcher, containerTypeMask, m_caster, m_caster, radius);


        CallScriptObjectAreaTargetSelectHandlers(targets, effIndex, targetType);

        if (!targets.empty())
        {
            // Other special target selection goes here
            if (uint32 maxTargets = m_spellValue->MaxAffectedTargets)
            {
                Unit::AuraEffectList const& Auras = m_caster->GetAuraEffectsByType(SPELL_AURA_MOD_MAX_AFFECTED_TARGETS);
                for (Unit::AuraEffectList::const_iterator j = Auras.begin(); j != Auras.end(); ++j)
                    if ((*j)->IsAffectedOnSpell(m_spellInfo))
                        maxTargets += (*j)->GetAmount();

                Trinity::Containers::RandomResize(targets, maxTargets);
            }

            for (std::list<WorldObject*>::iterator itr = targets.begin(); itr != targets.end(); ++itr)
            {
                if (Unit* unitTarget = (*itr)->ToUnit())
                    AddUnitTarget(unitTarget, effMask, false);
                else if (GameObject* gObjTarget = (*itr)->ToGameObject())
                    AddGOTarget(gObjTarget, effMask);
            }
        }
    }
}

void Spell::SelectImplicitAreaTargets(SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType, uint32 effMask)
{
    Unit* referer = NULL;
    switch (targetType.GetReferenceType())
    {
        case TARGET_REFERENCE_TYPE_SRC:
        case TARGET_REFERENCE_TYPE_DEST:
        case TARGET_REFERENCE_TYPE_CASTER:
            referer = m_caster;
            break;
        case TARGET_REFERENCE_TYPE_TARGET:
            referer = m_targets.GetUnitTarget();
            break;
        case TARGET_REFERENCE_TYPE_LAST:
        {
            // find last added target for this effect
            for (std::list<TargetInfo>::reverse_iterator ihit = m_UniqueTargetInfo.rbegin(); ihit != m_UniqueTargetInfo.rend(); ++ihit)
            {
                if (ihit->effectMask & (1<<effIndex))
                {
                    referer = ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID);
                    break;
                }
            }
            break;
        }
        default:
            ASSERT(false && "Spell::SelectImplicitAreaTargets: received not implemented target reference type");
            return;
    }
    if (!referer)
        return;

    Position const* center = NULL;
    switch (targetType.GetReferenceType())
    {
        case TARGET_REFERENCE_TYPE_SRC:
            center = m_targets.GetSrcPos();
            break;
        case TARGET_REFERENCE_TYPE_DEST:
            center = m_targets.GetDstPos();
            break;
        case TARGET_REFERENCE_TYPE_CASTER:
        case TARGET_REFERENCE_TYPE_TARGET:
        case TARGET_REFERENCE_TYPE_LAST:
            center = referer;
            break;
         default:
             ASSERT(false && "Spell::SelectImplicitAreaTargets: received not implemented target reference type");
             return;
    }
    std::list<WorldObject*> targets;
    float radius = m_spellInfo->Effects[effIndex].CalcRadius(m_caster) * m_spellValue->RadiusMod;
    SearchAreaTargets(targets, radius, center, referer, targetType.GetObjectType(), targetType.GetCheckType(), m_spellInfo->Effects[effIndex].ImplicitTargetConditions);

    CallScriptObjectAreaTargetSelectHandlers(targets, effIndex, targetType);

    if (!targets.empty())
    {
        // Other special target selection goes here
        if (uint32 maxTargets = m_spellValue->MaxAffectedTargets)
        {
            Unit::AuraEffectList const& Auras = m_caster->GetAuraEffectsByType(SPELL_AURA_MOD_MAX_AFFECTED_TARGETS);
            for (Unit::AuraEffectList::const_iterator j = Auras.begin(); j != Auras.end(); ++j)
                if ((*j)->IsAffectedOnSpell(m_spellInfo))
                    maxTargets += (*j)->GetAmount();

            Trinity::Containers::RandomResize(targets, maxTargets);
        }

        for (std::list<WorldObject*>::iterator itr = targets.begin(); itr != targets.end(); ++itr)
        {
            if (Unit* unitTarget = (*itr)->ToUnit())
                AddUnitTarget(unitTarget, effMask, false);
            else if (GameObject* gObjTarget = (*itr)->ToGameObject())
                AddGOTarget(gObjTarget, effMask);
        }
    }
}

void Spell::SelectImplicitCasterDestTargets(SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType)
{
    SpellDestination dest(*m_caster);

    switch (targetType.GetTarget())
    {
        case TARGET_DEST_CASTER:
            break;
        case TARGET_DEST_HOME:
            if (Player* playerCaster = m_caster->ToPlayer())
                dest = SpellDestination(playerCaster->m_homebindX, playerCaster->m_homebindY, playerCaster->m_homebindZ, playerCaster->GetOrientation(), playerCaster->m_homebindMapId);
            break;
        case TARGET_DEST_DB:
            if (SpellTargetPosition const* st = sSpellMgr->GetSpellTargetPosition(m_spellInfo->Id, effIndex))
            {
                /// @todo fix this check
                if (m_spellInfo->HasEffect(SPELL_EFFECT_TELEPORT_UNITS) || m_spellInfo->HasEffect(SPELL_EFFECT_BIND))
                    dest = SpellDestination(st->target_X, st->target_Y, st->target_Z, st->target_Orientation, (int32)st->target_mapId);
                else if (st->target_mapId == m_caster->GetMapId())
                    dest = SpellDestination(st->target_X, st->target_Y, st->target_Z, st->target_Orientation);
            }
            else
            {
                TC_LOG_DEBUG("spells", "SPELL: unknown target coordinates for spell ID %u", m_spellInfo->Id);
                if (WorldObject* target = m_targets.GetObjectTarget())
                    dest = SpellDestination(*target);
            }
            break;
        case TARGET_DEST_CASTER_FISHING:
        {
            float minDist = m_spellInfo->GetMinRange(true);
            float maxDist = m_spellInfo->GetMaxRange(true);
            float dist = frand(minDist, maxDist);
            float x, y, z;
            float angle = float(rand_norm()) * static_cast<float>(M_PI * 35.0f / 180.0f) - static_cast<float>(M_PI * 17.5f / 180.0f);
            m_caster->GetClosePoint(x, y, z, DEFAULT_WORLD_OBJECT_SIZE, dist, angle);

            float ground = m_caster->GetMap()->GetHeight(m_caster->GetPhaseMask(), x, y, z, true, 50.0f);
            float liquidLevel = VMAP_INVALID_HEIGHT_VALUE;
            LiquidData liquidData;
            if (m_caster->GetMap()->getLiquidStatus(x, y, z, MAP_ALL_LIQUIDS, &liquidData))
                liquidLevel = liquidData.level;

            if (liquidLevel <= ground) // When there is no liquid Map::GetWaterOrGroundLevel returns ground level
            {
                SendCastResult(SPELL_FAILED_NOT_HERE);
                SendChannelUpdate(0);
                finish(false);
                return;
            }

            if (ground + 0.75 > liquidLevel)
            {
                SendCastResult(SPELL_FAILED_TOO_SHALLOW);
                SendChannelUpdate(0);
                finish(false);
                return;
            }

            dest = SpellDestination(x, y, liquidLevel, m_caster->GetOrientation());
            break;
        }
        case TARGET_DEST_CASTER_FRONT_LEAP:
        {
            float distance = m_spellInfo->Effects[effIndex].CalcRadius(m_caster);
            Map* map = m_caster->GetMap();
            uint32 mapid = m_caster->GetMapId();
            uint32 phasemask = m_caster->GetPhaseMask();
            float destx, desty, destz, ground;

            Position pos;
            Position lastpos;
            m_caster->GetPosition(&pos);
            destx = pos.GetPositionX() + distance * cos(pos.GetOrientation());
            desty = pos.GetPositionY() + distance * sin(pos.GetOrientation());
            destz = pos.GetPositionZ();

            ground = map->GetHeight(phasemask, pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());

            if (!m_caster->HasUnitMovementFlag(MOVEMENTFLAG_FALLING) || (pos.GetPositionZ() - ground < 25.0f))
            {
                float tstX, tstY, tstZ, prevX, prevY, prevZ;
                float tstZ1, tstZ2, tstZ3, destz1, destz2, destz3, srange = 0.0, srange1, srange2, srange3;
                float maxtravelDistZ = 2.65f;
                float overdistance = 0.0f;
                float totalpath = 0.0f;
                float beforewaterz = 0.0f;
                const float  step = 2.0f;
                const uint8 numChecks = ceil(fabs(distance / step));
                const float DELTA_X = (destx - pos.GetPositionX()) / numChecks;
                const float DELTA_Y = (desty - pos.GetPositionY()) / numChecks;
                int j = 1;
                for (; j < (numChecks + 1); j++)
                {
                    prevX = pos.GetPositionX() + (float(j - 1)*DELTA_X);
                    prevY = pos.GetPositionY() + (float(j - 1)*DELTA_Y);
                    tstX = pos.GetPositionX() + (float(j)*DELTA_X);
                    tstY = pos.GetPositionY() + (float(j)*DELTA_Y);

                    if (j < 2)
                    {
                        prevZ = pos.GetPositionZ();
                    }
                    else
                    {
                        prevZ = tstZ;
                    }

                    tstZ = map->GetHeight(phasemask, tstX, tstY, prevZ + maxtravelDistZ, true);
                    ground = tstZ;

                    if (!map->IsInWater(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()))
                    {
                        if (map->IsInWater(tstX, tstY, tstZ))
                        {
                            if (!(beforewaterz != 0.0f))
                                beforewaterz = prevZ;
                            tstZ = beforewaterz;
                            srange = sqrt((tstY - prevY)*(tstY - prevY) + (tstX - prevX)*(tstX - prevX));
                            //TC_LOG_ERROR("server", "(start was from land) step in water , number of cycle = %i , distance of step = %f, total path = %f, Z = %f", j, srange, totalpath, tstZ);
                        }
                    }
                    else if (map->IsInWater(tstX, tstY, tstZ))
                    {
                        prevZ = pos.GetPositionZ();
                        tstZ = pos.GetPositionZ();
                        srange = sqrt((tstY - prevY)*(tstY - prevY) + (tstX - prevX)*(tstX - prevX));
                        //TC_LOG_ERROR("server", "(start in water) step in water, number of cycle = %i , distance of step = %f, total path = %f", j, srange, totalpath);
                    }

                    if (!map->IsInWater(tstX, tstY, tstZ) && tstZ != beforewaterz)  // second safety check z for blink way if on the ground
                    {
                        // highest available point
                        tstZ1 = map->GetHeight(phasemask, tstX, tstY, prevZ + maxtravelDistZ, true, 25.0f);
                        // upper or floor
                        tstZ2 = map->GetHeight(phasemask, tstX, tstY, prevZ, true, 25.0f);
                        //lower than floor
                        tstZ3 = map->GetHeight(phasemask, tstX, tstY, prevZ - maxtravelDistZ / 2, true, 25.0f);

                        //distance of rays, will select the shortest in 3D
                        srange1 = sqrt((tstY - prevY)*(tstY - prevY) + (tstX - prevX)*(tstX - prevX) + (tstZ1 - prevZ)*(tstZ1 - prevZ));
                        //TC_LOG_ERROR("server", "step = %i, distance of ray1 = %f", j, srange1);
                        srange2 = sqrt((tstY - prevY)*(tstY - prevY) + (tstX - prevX)*(tstX - prevX) + (tstZ2 - prevZ)*(tstZ2 - prevZ));
                        //TC_LOG_ERROR("server", "step = %i, distance of ray2 = %f", j, srange2);
                        srange3 = sqrt((tstY - prevY)*(tstY - prevY) + (tstX - prevX)*(tstX - prevX) + (tstZ3 - prevZ)*(tstZ3 - prevZ));
                        //TC_LOG_ERROR("server", "step = %i, distance of ray3 = %f", j, srange3);

                        if (srange1 < srange2)
                            tstZ = tstZ1;
                        else if (srange3 < srange2)
                            tstZ = tstZ3;
                        else
                            tstZ = tstZ2;

                        srange = sqrt((tstY - prevY)*(tstY - prevY) + (tstX - prevX)*(tstX - prevX) + (tstZ - prevZ)*(tstZ - prevZ));
                        //TC_LOG_ERROR("server", "step on ground, number of cycle = %i , distance of step = %f, total path = %f", j, srange, totalpath);
                    }

                    destx = tstX;
                    desty = tstY;
                    destz = tstZ;

                    totalpath += srange;

                    if (totalpath > distance)
                    {
                        overdistance = totalpath - distance;
                        //TC_LOG_ERROR("server", "total path > than distance in 3D , need to move back a bit for save distance, total path = %f, overdistance = %f", totalpath, overdistance);
                    }

                    bool col = VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(mapid, prevX, prevY, prevZ + 0.5f, tstX, tstY, tstZ + 0.5f, tstX, tstY, tstZ, -0.5f);
                    // check dynamic collision
                    bool dcol = m_caster->GetMap()->getDynamicObjectHitPos(phasemask, prevX, prevY, prevZ + 0.5f, tstX, tstY, tstZ + 0.5f, tstX, tstY, tstZ, -0.5f);

                    // collision occured
                    if (col || dcol || (overdistance > 0.0f && !map->IsInWater(tstX, tstY, ground)) || (fabs(prevZ - tstZ) > maxtravelDistZ && (tstZ > prevZ)))
                    {
                        if ((overdistance > 0.0f) && (overdistance < step))
                        {
                            destx = prevX + overdistance * cos(pos.GetOrientation());
                            desty = prevY + overdistance * sin(pos.GetOrientation());
                            //TC_LOG_ERROR("server", "(collision) collision occured 1");
                        }
                        else
                        {
                            // move back a bit
                            destx = tstX - (0.6 * cos(pos.GetOrientation()));
                            desty = tstY - (0.6 * sin(pos.GetOrientation()));
                            //TC_LOG_ERROR("server", "(collision) collision occured 2");
                        }

                        // highest available point
                        destz1 = map->GetHeight(phasemask, destx, desty, prevZ + maxtravelDistZ, true, 25.0f);
                        // upper or floor
                        destz2 = map->GetHeight(phasemask, destx, desty, prevZ, true, 25.0f);
                        //lower than floor
                        destz3 = map->GetHeight(phasemask, destx, desty, prevZ - maxtravelDistZ / 2, true, 25.0f);

                        //distance of rays, will select the shortest in 3D
                        srange1 = sqrt((desty - prevY)*(desty - prevY) + (destx - prevX)*(destx - prevX) + (destz1 - prevZ)*(destz1 - prevZ));
                        srange2 = sqrt((desty - prevY)*(desty - prevY) + (destx - prevX)*(destx - prevX) + (destz2 - prevZ)*(destz2 - prevZ));
                        srange3 = sqrt((desty - prevY)*(desty - prevY) + (destx - prevX)*(destx - prevX) + (destz3 - prevZ)*(destz3 - prevZ));

                        if (srange1 < srange2)
                            destz = destz1;
                        else if (srange3 < srange2)
                            destz = destz3;
                        else
                            destz = destz2;

                        //TC_LOG_ERROR("server", "(collision) destZ rewrited in prevZ");                         

                        break;
                    }
                    // we have correct destz now
                }
                //}

                lastpos.Relocate(destx, desty, destz + 0.5f, pos.GetOrientation());
                dest = SpellDestination(lastpos);
            }
            else
            {
                float z = pos.GetPositionZ();
                bool col = VMAP::VMapFactory::createOrGetVMapManager()->getObjectHitPos(mapid, pos.GetPositionX(), pos.GetPositionY(), z + 0.5f, destx, desty, z + 0.5f, destx, desty, z, -0.5f);
                // check dynamic collision
                bool dcol = m_caster->GetMap()->getDynamicObjectHitPos(phasemask, pos.GetPositionX(), pos.GetPositionY(), z + 0.5f, destx, desty, z + 0.5f, destx, desty, z, -0.5f);

                // collision occured
                if (col || dcol)
                {
                    // move back a bit
                    destx = destx - (0.6 * cos(pos.GetOrientation()));
                    desty = desty - (0.6 * sin(pos.GetOrientation()));
                }

                lastpos.Relocate(destx, desty, z, pos.GetOrientation());
                dest = SpellDestination(lastpos);
                //float range = sqrt((desty - pos.GetPositionY())*(desty - pos.GetPositionY()) + (destx - pos.GetPositionX())*(destx - pos.GetPositionX()));
                //TC_LOG_ERROR("server", "Blink number 2, in falling but at a hight, distance of blink = %f", range);
            }

            break;
        }
        default:
        {
            const float objSize = m_caster->GetObjectSize();

            float dist = 0.0f;

            switch (targetType.GetTarget())
            {
                case TARGET_DEST_CASTER_SUMMON:
                    dist = PET_FOLLOW_DIST;
                    break;
                case TARGET_DEST_CASTER_RANDOM:
                    dist = objSize + (dist - objSize) * float(rand_norm());
                    break;
                default:
                    if (m_spellInfo->Effects[effIndex].BasePoints)
                        dist = m_spellInfo->Effects[effIndex].CalcRadius(m_caster);
                    if (dist <= 0.0f)
                        dist = objSize + 1.0f;
                    break;
            }

            const float angle = targetType.CalcDirectionAngle();

            Position pos = dest._position;
            pos = m_caster->MovePositionToFirstCollision2D(pos, dist, angle + m_caster->GetOrientation());

            dest.Relocate(pos);
            break;
        }
    }

    CallScriptDestinationTargetSelectHandlers(dest, effIndex, targetType);
    m_targets.SetDst(dest);
}

void Spell::SelectImplicitTargetDestTargets(SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType)
{
    WorldObject* target = m_targets.GetObjectTarget();

    SpellDestination dest(*target);

    switch (targetType.GetTarget())
    {
        case TARGET_DEST_TARGET_ENEMY:
        case TARGET_DEST_TARGET_ANY:
            break;
        default:
        {
            float angle = targetType.CalcDirectionAngle();
            float objSize = target->GetObjectSize();
            float dist = m_spellInfo->Effects[effIndex].CalcRadius(m_caster);
            if (dist < objSize)
                dist = objSize;
            else if (targetType.GetTarget() == TARGET_DEST_TARGET_RANDOM)
                dist = objSize + (dist - objSize) * float(rand_norm());

            Position pos = dest._position;
            pos = target->MovePositionToFirstCollision2D(pos, dist, angle + target->GetOrientation());

            dest.Relocate(pos);
            break;
        }
    }

    CallScriptDestinationTargetSelectHandlers(dest, effIndex, targetType);
    m_targets.SetDst(dest);
}

void Spell::SelectImplicitDestDestTargets(SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType)
{
    // set destination to caster if no dest provided
    // can only happen if previous destination target could not be set for some reason
    // (not found nearby target, or channel target for example
    // maybe we should abort the spell in such case?
    CheckDst();

    SpellDestination dest(*m_targets.GetDst());

    switch (targetType.GetTarget())
    {
        case TARGET_DEST_DYNOBJ_ENEMY:
        case TARGET_DEST_DYNOBJ_ALLY:
        case TARGET_DEST_DYNOBJ_NONE:
        case TARGET_DEST_DEST:
            return;
        case TARGET_DEST_TRAJ:
            SelectImplicitTrajTargets(effIndex);
            return;
        default:
        {
            float angle = targetType.CalcDirectionAngle();
            float dist = m_spellInfo->Effects[effIndex].CalcRadius(m_caster);
            if (targetType.GetTarget() == TARGET_DEST_DEST_RANDOM)
                dist *= float(rand_norm());

            Position pos = dest._position;
            pos = m_caster->MovePositionToFirstCollision2D(pos, dist, angle + m_caster->GetOrientation());

            dest.Relocate(pos);
            break;
        }
    }

    CallScriptDestinationTargetSelectHandlers(dest, effIndex, targetType);
    m_targets.ModDst(dest);
}

void Spell::SelectImplicitCasterObjectTargets(SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType)
{
    WorldObject* target = NULL;
    bool checkIfValid = true;

    switch (targetType.GetTarget())
    {
        case TARGET_UNIT_CASTER:
            target = m_caster;
            checkIfValid = false;
            break;
        case TARGET_UNIT_MASTER:
            target = m_caster->GetCharmerOrOwner();
            break;
        case TARGET_UNIT_PET:
            target = m_caster->GetGuardianPet();
            break;
        case TARGET_UNIT_SUMMONER:
            if (m_caster->IsSummon())
                target = m_caster->ToTempSummon()->GetSummoner();
            break;
        case TARGET_UNIT_VEHICLE:
            target = m_caster->GetVehicleBase();
            break;
        case TARGET_UNIT_PASSENGER_0:
        case TARGET_UNIT_PASSENGER_1:
        case TARGET_UNIT_PASSENGER_2:
        case TARGET_UNIT_PASSENGER_3:
        case TARGET_UNIT_PASSENGER_4:
        case TARGET_UNIT_PASSENGER_5:
        case TARGET_UNIT_PASSENGER_6:
        case TARGET_UNIT_PASSENGER_7:
            if (m_caster->GetTypeId() == TYPEID_UNIT && m_caster->IsVehicle())
                target = m_caster->GetVehicleKit()->GetPassenger(targetType.GetTarget() - TARGET_UNIT_PASSENGER_0);
            break;
        default:
            break;
    }

    CallScriptObjectTargetSelectHandlers(target, effIndex, targetType);

    if (target && target->ToUnit())
        AddUnitTarget(target->ToUnit(), 1 << effIndex, checkIfValid);
}

void Spell::SelectImplicitTargetObjectTargets(SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType)
{
    ASSERT((m_targets.GetObjectTarget() || m_targets.GetItemTarget()) && "Spell::SelectImplicitTargetObjectTargets - no explicit object or item target available!");

    WorldObject* target = m_targets.GetObjectTarget();

    CallScriptObjectTargetSelectHandlers(target, effIndex, targetType);

    if (target)
    {
        if (Unit* unit = target->ToUnit())
            AddUnitTarget(unit, 1 << effIndex, true, false);
        else if (GameObject* gobj = target->ToGameObject())
            AddGOTarget(gobj, 1 << effIndex);

        SelectImplicitChainTargets(effIndex, targetType, target, 1 << effIndex);
    }
    // Script hook can remove object target and we would wrongly land here
    else if (Item* item = m_targets.GetItemTarget())
        AddItemTarget(item, 1 << effIndex);
}

void Spell::SelectImplicitChainTargets(SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType, WorldObject* target, uint32 effMask)
{
    uint32 maxTargets = m_spellInfo->Effects[effIndex].ChainTarget;
    if (Player* modOwner = m_caster->GetSpellModOwner())
        modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_JUMP_TARGETS, maxTargets, this);

    if (maxTargets > 1)
    {
        // mark damage multipliers as used
        for (uint32 k = effIndex; k < MAX_SPELL_EFFECTS; ++k)
            if (effMask & (1 << k))
                m_damageMultipliers[k] = 1.0f;
        m_applyMultiplierMask |= effMask;

        std::list<WorldObject*> targets;
        SearchChainTargets(targets, maxTargets - 1, target, targetType.GetObjectType(), targetType.GetCheckType()
            , m_spellInfo->Effects[effIndex].ImplicitTargetConditions, targetType.GetTarget() == TARGET_UNIT_TARGET_CHAINHEAL_ALLY);

        // Chain primary target is added earlier
        CallScriptObjectAreaTargetSelectHandlers(targets, effIndex, targetType);

        for (std::list<WorldObject*>::iterator itr = targets.begin(); itr != targets.end(); ++itr)
            if (Unit* unitTarget = (*itr)->ToUnit())
                AddUnitTarget(unitTarget, effMask, false);
    }
}

float tangent(float x)
{
    x = std::tan(x);
    //if (x < std::numeric_limits<float>::max() && x > -std::numeric_limits<float>::max()) return x;
    //if (x >= std::numeric_limits<float>::max()) return std::numeric_limits<float>::max();
    //if (x <= -std::numeric_limits<float>::max()) return -std::numeric_limits<float>::max();
    if (x < 100000.0f && x > -100000.0f) return x;
    if (x >= 100000.0f) return 100000.0f;
    if (x <= 100000.0f) return -100000.0f;
    return 0.0f;
}

#define DEBUG_TRAJ(a) //a

void Spell::SelectImplicitTrajTargets(SpellEffIndex effIndex)
{
    if (!m_targets.HasTraj())
        return;

    float dist2d = m_targets.GetDist2d();
    if (!dist2d)
        return;

    float srcToDestDelta = m_targets.GetDstPos()->m_positionZ - m_targets.GetSrcPos()->m_positionZ;

    std::list<WorldObject*> targets;
    Trinity::WorldObjectSpellTrajTargetCheck check(dist2d, m_targets.GetSrcPos(), m_caster, m_spellInfo);

    auto searcher = Trinity::createBaseObjectSearcher<WorldObject, Trinity::ContainerAction>(m_caster, targets, check);
    SearchTargets(searcher, GRID_MAP_TYPE_MASK_ALL, m_caster, m_targets.GetSrcPos(), dist2d);
    if (targets.empty())
        return;

    targets.sort(Trinity::ObjectDistanceOrderPred(m_caster));

    float b = tangent(m_targets.GetElevation());
    float a = (srcToDestDelta - dist2d * b) / (dist2d * dist2d);
    if (a > -0.0001f)
        a = 0;
    DEBUG_TRAJ(TC_LOG_ERROR("spells", "Spell::SelectTrajTargets: a %f b %f", a, b);)

    float bestDist = m_spellInfo->GetMaxRange(false);

    std::list<WorldObject*>::const_iterator itr = targets.begin();
    for (; itr != targets.end(); ++itr)
    {
        if (!m_caster->HasInLine(*itr, 5.0f))
            continue;

        if (m_spellInfo->CheckTarget(m_caster, *itr, true) != SPELL_CAST_OK)
            continue;

        if (Unit* unitTarget = (*itr)->ToUnit())
        {
            if (m_caster == *itr || m_caster->IsOnVehicle(unitTarget) || unitTarget->GetVehicle())
                continue;

            if (Creature* creatureTarget = unitTarget->ToCreature())
            {
                if (!(creatureTarget->GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_CAN_COLLIDE_WITH_MISSILES))
                    continue;
            }

            // Pyrit shouldn't hit Triggers (nor pool of tars), not sure about all the 103 other Spells using this though...
            if (m_spellInfo->Id == 62490 && (*itr)->ToCreature())
                if (((*itr)->ToCreature()->IsTrigger()) || ((*itr)->ToCreature()->GetEntry() == 33090) || (!(*itr)->ToCreature()->IsAlive()) || (*itr)->ToCreature()->IsFriendlyTo(m_caster))
                    continue;
        }

        const float size = std::max((*itr)->GetObjectSize() * 0.7f, 1.0f); // 1/sqrt(3)
        /// @todo all calculation should be based on src instead of m_caster
        const float objDist2d = m_targets.GetSrcPos()->GetExactDist2d(*itr) * std::cos(m_targets.GetSrcPos()->GetRelativeAngle(*itr));
        const float dz = (*itr)->GetPositionZ() - m_targets.GetSrcPos()->m_positionZ;

        DEBUG_TRAJ(TC_LOG_ERROR("spells", "Spell::SelectTrajTargets: check %u, dist between %f %f, height between %f %f.", (*itr)->GetEntry(), objDist2d - size, objDist2d + size, dz - size, dz + size);)

        float dist = objDist2d - size;
        float height = dist * (a * dist + b);
        DEBUG_TRAJ(TC_LOG_ERROR("spells", "Spell::SelectTrajTargets: dist %f, height %f.", dist, height);)
        if (dist < bestDist && height < dz + size && height > dz - size)
        {
            bestDist = dist > 0 ? dist : 0;
            break;
        }

#define CHECK_DIST {\
            DEBUG_TRAJ(TC_LOG_ERROR("spells", "Spell::SelectTrajTargets: dist %f, height %f.", dist, height);)\
            if (dist > bestDist)\
                continue;\
            if (dist < objDist2d + size && dist > objDist2d - size)\
            {\
                bestDist = dist;\
                break;\
            }\
        }

        if (!a)
        {
            height = dz - size;
            dist = height / b;
            CHECK_DIST;

            height = dz + size;
            dist = height / b;
            CHECK_DIST;

            continue;
        }

        height = dz - size;
        float sqrt1 = b * b + 4 * a * height;
        if (sqrt1 > 0)
        {
            sqrt1 = std::sqrt(sqrt1);
            dist = (sqrt1 - b) / (2 * a);
            CHECK_DIST;
        }

        height = dz + size;
        float sqrt2 = b * b + 4 * a * height;
        if (sqrt2 > 0)
        {
            sqrt2 = std::sqrt(sqrt2);
            dist = (sqrt2 - b) / (2 * a);
            CHECK_DIST;

            dist = (-sqrt2 - b) / (2 * a);
            CHECK_DIST;
        }

        if (sqrt1 > 0)
        {
            dist = (-sqrt1 - b) / (2 * a);
            CHECK_DIST;
        }
    }

    if (m_targets.GetSrcPos()->GetExactDist2d(m_targets.GetDstPos()) > bestDist)
    {
        float x = m_targets.GetSrcPos()->m_positionX + std::cos(m_caster->GetOrientation()) * bestDist;
        float y = m_targets.GetSrcPos()->m_positionY + std::sin(m_caster->GetOrientation()) * bestDist;
        float z = m_targets.GetSrcPos()->m_positionZ + bestDist * (a * bestDist + b);

        if (itr != targets.end())
        {
            float distSq = (*itr)->GetExactDistSq(x, y, z);
            float sizeSq = (*itr)->GetObjectSize();
            sizeSq *= sizeSq;
            DEBUG_TRAJ(TC_LOG_ERROR("spells", "Initial %f %f %f %f %f", x, y, z, distSq, sizeSq);)
            if (distSq > sizeSq)
            {
                float factor = 1 - std::sqrt(sizeSq / distSq);
                x += factor * ((*itr)->GetPositionX() - x);
                y += factor * ((*itr)->GetPositionY() - y);
                z += factor * ((*itr)->GetPositionZ() - z);

                distSq = (*itr)->GetExactDistSq(x, y, z);
                DEBUG_TRAJ(TC_LOG_ERROR("spells", "Initial %f %f %f %f %f", x, y, z, distSq, sizeSq);)
            }
        }

        Position trajDst;
        trajDst.Relocate(x, y, z, m_caster->GetOrientation());
        SpellDestination dest(*m_targets.GetDst());
        dest.Relocate(trajDst);

        CallScriptDestinationTargetSelectHandlers(dest, effIndex, SpellImplicitTargetInfo(TARGET_DEST_TRAJ));
        m_targets.ModDst(dest);
    }
}

void Spell::SelectEffectTypeImplicitTargets(uint8 effIndex)
{
    // special case for SPELL_EFFECT_SUMMON_RAF_FRIEND and SPELL_EFFECT_SUMMON_PLAYER
    /// @todo this is a workaround - target shouldn't be stored in target map for those spells
    switch (m_spellInfo->Effects[effIndex].Effect)
    {
        case SPELL_EFFECT_SUMMON_RAF_FRIEND:
        case SPELL_EFFECT_SUMMON_PLAYER:
            if (m_caster->GetTypeId() == TYPEID_PLAYER && m_caster->GetTarget())
            {
                WorldObject* target = ObjectAccessor::FindPlayer(m_caster->GetTarget());

                CallScriptObjectTargetSelectHandlers(target, SpellEffIndex(effIndex), SpellImplicitTargetInfo());

                if (target && target->ToPlayer())
                    AddUnitTarget(target->ToUnit(), 1 << effIndex, false);
            }
            return;
        default:
            break;
    }

    // select spell implicit targets based on effect type
    if (!m_spellInfo->Effects[effIndex].GetImplicitTargetType())
        return;

    uint32 targetMask = m_spellInfo->Effects[effIndex].GetMissingTargetMask();

    if (!targetMask)
        return;

    WorldObject* target = NULL;

    switch (m_spellInfo->Effects[effIndex].GetImplicitTargetType())
    {
        // add explicit object target or self to the target map
        case EFFECT_IMPLICIT_TARGET_EXPLICIT:
            // player which not released his spirit is Unit, but target flag for it is TARGET_FLAG_CORPSE_MASK
            if (targetMask & (TARGET_FLAG_UNIT_MASK | TARGET_FLAG_CORPSE_MASK))
            {
                if (Unit* unitTarget = m_targets.GetUnitTarget())
                    target = unitTarget;
                else if (targetMask & TARGET_FLAG_CORPSE_MASK)
                {
                    if (Corpse* corpseTarget = m_targets.GetCorpseTarget())
                    {
                        /// @todo this is a workaround - corpses should be added to spell target map too, but we can't do that so we add owner instead
                        if (Player* owner = ObjectAccessor::FindPlayer(corpseTarget->GetOwnerGUID()))
                            target = owner;
                    }
                }
                else //if (targetMask & TARGET_FLAG_UNIT_MASK)
                    target = m_caster;
            }
            if (targetMask & TARGET_FLAG_ITEM_MASK)
            {
                if (Item* itemTarget = m_targets.GetItemTarget())
                    AddItemTarget(itemTarget, 1 << effIndex);
                return;
            }
            if (targetMask & TARGET_FLAG_GAMEOBJECT_MASK)
                target = m_targets.GetGOTarget();
            break;
        // add self to the target map
        case EFFECT_IMPLICIT_TARGET_CASTER:
            if (targetMask & TARGET_FLAG_UNIT_MASK)
                target = m_caster;
            break;
        default:
            break;
    }

    CallScriptObjectTargetSelectHandlers(target, SpellEffIndex(effIndex), SpellImplicitTargetInfo());

    if (target)
    {
        if (target->ToUnit())
            AddUnitTarget(target->ToUnit(), 1 << effIndex, false);
        else if (target->ToGameObject())
            AddGOTarget(target->ToGameObject(), 1 << effIndex);
    }
}

uint32 Spell::GetSearcherTypeMask(SpellTargetObjectTypes objType, ConditionContainer* condList)
{
    // this function selects which containers need to be searched for spell target
    uint32 retMask = GRID_MAP_TYPE_MASK_ALL;

    // filter searchers based on searched object type
    switch (objType)
    {
        case TARGET_OBJECT_TYPE_UNIT:
        case TARGET_OBJECT_TYPE_UNIT_AND_DEST:
        case TARGET_OBJECT_TYPE_CORPSE:
        case TARGET_OBJECT_TYPE_CORPSE_ENEMY:
        case TARGET_OBJECT_TYPE_CORPSE_ALLY:
            retMask &= GRID_MAP_TYPE_MASK_PLAYER | GRID_MAP_TYPE_MASK_CORPSE | GRID_MAP_TYPE_MASK_CREATURE;
            break;
        case TARGET_OBJECT_TYPE_GOBJ:
        case TARGET_OBJECT_TYPE_GOBJ_ITEM:
            retMask &= GRID_MAP_TYPE_MASK_GAMEOBJECT;
            break;
        default:
            break;
    }
    if (!m_spellInfo->HasAttribute(SPELL_ATTR2_CAN_TARGET_DEAD))
        retMask &= ~GRID_MAP_TYPE_MASK_CORPSE;
    if (m_spellInfo->HasAttribute(SPELL_ATTR3_ONLY_TARGET_PLAYERS))
        retMask &= GRID_MAP_TYPE_MASK_CORPSE | GRID_MAP_TYPE_MASK_PLAYER;
    if (m_spellInfo->HasAttribute(SPELL_ATTR3_ONLY_TARGET_GHOSTS))
        retMask &= GRID_MAP_TYPE_MASK_PLAYER;

    if (condList)
        retMask &= sConditionMgr->GetSearcherTypeMaskForConditionList(*condList);
    return retMask;
}

template<class SEARCHER>
void Spell::SearchTargets(SEARCHER& searcher, uint32 containerMask, Unit* referer, Position const* pos, float radius)
{
    if (!containerMask)
        return;

    // search world and grid for possible targets
    bool searchInGrid = (containerMask & (GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_GAMEOBJECT)) != 0;
    bool searchInWorld = (containerMask & (GRID_MAP_TYPE_MASK_CREATURE | GRID_MAP_TYPE_MASK_PLAYER | GRID_MAP_TYPE_MASK_CORPSE)) != 0;
    if (searchInGrid || searchInWorld)
    {
        float x, y;
        x = pos->GetPositionX();
        y = pos->GetPositionY();
        
        Map& map = *(referer->GetMap());
        map.Visit(x, y, radius, searcher);
    }
}

WorldObject* Spell::SearchNearbyTarget(float range, SpellTargetObjectTypes objectType, SpellTargetCheckTypes selectionType, ConditionContainer* condList)
{
    WorldObject* target = NULL;
    uint32 containerTypeMask = GetSearcherTypeMask(objectType, condList);
    if (!containerTypeMask)
        return NULL;

    Trinity::WorldObjectSpellNearbyTargetCheck check(range, m_caster, m_spellInfo, selectionType, condList);
    Trinity::MapTypeMaskCheck mask(containerTypeMask);

    auto searcher = Trinity::createBaseObjectSearcher<WorldObject, Trinity::LastAction>(m_caster, target, check, mask);
    SearchTargets(searcher, containerTypeMask, m_caster, m_caster, range);
    return target;
}

void Spell::SearchAreaTargets(std::list<WorldObject*>& targets, float range, Position const* position, Unit* referer, SpellTargetObjectTypes objectType, SpellTargetCheckTypes selectionType, ConditionContainer* condList)
{
    uint32 containerTypeMask = GetSearcherTypeMask(objectType, condList);
    if (!containerTypeMask)
        return;
    Trinity::WorldObjectSpellAreaTargetCheck check(range, position, m_caster, referer, m_spellInfo, selectionType, condList);
    Trinity::MapTypeMaskCheck mask(containerTypeMask);

    auto searcher = Trinity::createBaseObjectSearcher<WorldObject, Trinity::ContainerAction>(m_caster, targets, check, mask);
    SearchTargets(searcher, containerTypeMask, m_caster, position, range + Spell::MAX_HITBOX_RANGE);
}

void Spell::SearchChainTargets(std::list<WorldObject*>& targets, uint32 chainTargets, WorldObject* target, SpellTargetObjectTypes objectType, SpellTargetCheckTypes selectType, ConditionContainer* condList, bool isChainHeal)
{
    // max dist for jump target selection
    float jumpRadius = 0.0f;
    switch (m_spellInfo->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_RANGED:
            // 7.5y for multi shot
            jumpRadius = 7.5f;
            break;
        case SPELL_DAMAGE_CLASS_MELEE:
            // 5y for swipe, cleave and similar
            jumpRadius = 5.0f;
            break;
        case SPELL_DAMAGE_CLASS_NONE:
        case SPELL_DAMAGE_CLASS_MAGIC:
            // 12.5y for chain heal spell since 3.2 patch
            if (isChainHeal)
                jumpRadius = 12.5f;
            // 10y as default for magic chain spells
            else
                jumpRadius = 10.0f;
            break;
    }

    // chain lightning/heal spells and similar - allow to jump at larger distance and go out of los
    bool isBouncingFar = (m_spellInfo->HasAttribute(SPELL_ATTR4_AREA_TARGET_CHAIN)
        || m_spellInfo->DmgClass == SPELL_DAMAGE_CLASS_NONE
        || m_spellInfo->DmgClass == SPELL_DAMAGE_CLASS_MAGIC);

    // max dist which spell can reach
    float searchRadius = jumpRadius;
    if (isBouncingFar)
        searchRadius *= chainTargets;

    std::list<WorldObject*> tempTargets;
    SearchAreaTargets(tempTargets, searchRadius, target, m_caster, objectType, selectType, condList);
    tempTargets.remove(target);

    // remove targets which are always invalid for chain spells
    // for some spells allow only chain targets in front of caster (swipe for example)
    if (!isBouncingFar)
    {
        for (std::list<WorldObject*>::iterator itr = tempTargets.begin(); itr != tempTargets.end();)
        {
            std::list<WorldObject*>::iterator checkItr = itr++;
            if (!m_caster->HasInArc(static_cast<float>(M_PI), *checkItr))
                tempTargets.erase(checkItr);
        }
    }

    while (chainTargets)
    {
        // try to get unit for next chain jump
        std::list<WorldObject*>::iterator foundItr = tempTargets.end();
        // get unit with highest hp deficit in dist
        if (isChainHeal)
        {
            uint32 maxHPDeficit = 0;
            for (std::list<WorldObject*>::iterator itr = tempTargets.begin(); itr != tempTargets.end(); ++itr)
            {
                if (Unit* unitTarget = (*itr)->ToUnit())
                {
                    uint32 deficit = unitTarget->GetMaxHealth() - unitTarget->GetHealth();
                    bool pvpCheck = (!m_caster->ToPlayer() || !unitTarget->ToPlayer() || (!unitTarget->IsPvP() && !unitTarget->IsFFAPvP()) || m_caster->IsPvP() || m_caster->IsFFAPvP());
                    if ((deficit > maxHPDeficit || foundItr == tempTargets.end()) && target->IsWithinDist(unitTarget, jumpRadius) && target->IsWithinLOSInMap(unitTarget) && pvpCheck)
                    {
                        foundItr = itr;
                        maxHPDeficit = deficit;
                    }
                }
            }
        }
        // get closest object
        else
        {
            for (std::list<WorldObject*>::iterator itr = tempTargets.begin(); itr != tempTargets.end(); ++itr)
            {
                bool pvpCheck = (!m_caster->ToPlayer() || !(*itr)->ToPlayer() || m_caster->IsPvP() || m_caster->IsFFAPvP());
                if (foundItr == tempTargets.end())
                {
                    if ((!isBouncingFar || target->IsWithinDist(*itr, jumpRadius)) && target->IsWithinLOSInMap(*itr) && pvpCheck)
                        foundItr = itr;
                }
                else if (target->GetDistanceOrder(*itr, *foundItr) && target->IsWithinLOSInMap(*itr) && pvpCheck)
                    foundItr = itr;
            }
        }
        // not found any valid target - chain ends
        if (foundItr == tempTargets.end())
            break;
        target = *foundItr;
        tempTargets.erase(foundItr);
        targets.push_back(target);
        --chainTargets;
    }
}

GameObject* Spell::SearchSpellFocus()
{
    GameObject* focus = NULL;
    Trinity::GameObjectFocusCheck check(m_caster, m_spellInfo->RequiresSpellFocus);
    auto searcher = Trinity::createBaseObjectSearcher<GameObject, Trinity::FirstAction>(m_caster, focus, check);
    SearchTargets(searcher, GRID_MAP_TYPE_MASK_GAMEOBJECT, m_caster, m_caster, m_caster->GetVisibilityRange());
    return focus;
}

void Spell::prepareDataForTriggerSystem(AuraEffect const* /*triggeredByAura*/)
{
    //==========================================================================================
    // Now fill data for trigger system, need know:
    // can spell trigger another or not (m_canTrigger)
    // Create base triggers flags for Attacker and Victim (m_procAttacker, m_procVictim and m_procEx)
    //==========================================================================================

    m_procVictim = m_procAttacker = 0;
    m_procEx = PROC_EX_NONE;
    // Get data for type of attack and fill base info for trigger
    m_spellInfo->GetBaseProccFlags(m_procAttacker, m_procVictim, m_procEx, _triggeredCastFlags, m_attackType, m_caster, m_originalCaster);
    // Other trigger procflags are set in Spell::DoAllEffectOnTarget
    // Because spell positivity is dependant on target
}

void Spell::CleanupTargetList()
{
    m_UniqueTargetInfo.clear();
    m_UniqueGOTargetInfo.clear();
    m_UniqueItemInfo.clear();
    m_delayMoment = 0;
}

void Spell::AddUnitTarget(Unit* target, uint32 effectMask, bool checkIfValid /*= true*/, bool implicit /*= true*/)
{
    for (uint32 effIndex = 0; effIndex < MAX_SPELL_EFFECTS; ++effIndex)
        if (!m_spellInfo->Effects[effIndex].IsEffect() || !CheckEffectTarget(target, effIndex))
            effectMask &= ~(1 << effIndex);

    // no effects left
    if (!effectMask)
        return;

    if (checkIfValid)
        if (m_spellInfo->CheckTarget(m_caster, target, implicit) != SPELL_CAST_OK)
            return;

    if (!(*this <<= Query::ValidTargetForSpell.Bind(target)))
        return;

    // Check for effect immune skip if immuned
    for (uint32 effIndex = 0; effIndex < MAX_SPELL_EFFECTS; ++effIndex)
        if (target->IsImmunedToSpellEffect(m_spellInfo, effIndex))
            effectMask &= ~(1 << effIndex);

    ObjectGuid targetGUID = target->GetGUID();

    // Lookup target in already in list
    for (std::list<TargetInfo>::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if (targetGUID == ihit->targetGUID)             // Found in list
        {
            ihit->effectMask |= effectMask;             // Immune effects removed from mask
            ihit->scaleAura = false;
            if (m_auraScaleMask && ihit->effectMask == m_auraScaleMask && m_caster != target)
            {
                SpellInfo const* auraSpell = m_spellInfo->GetFirstRankSpell();
                if (uint32(target->getLevel() + 10) >= auraSpell->SpellLevel)
                    ihit->scaleAura = true;
            }
            return;
        }
    }

    // This is new target calculate data for him

    // Get spell hit result on target
    TargetInfo targetInfo;
    targetInfo.targetGUID = targetGUID;                         // Store target GUID
    targetInfo.effectMask = effectMask;                         // Store all effects not immune
    targetInfo.processed  = false;                              // Effects not apply on target
    targetInfo.alive      = target->IsAlive();
    targetInfo.damage     = 0;
    targetInfo.crit       = false;
    targetInfo.scaleAura  = false;
    if (m_auraScaleMask && targetInfo.effectMask == m_auraScaleMask && m_caster != target)
    {
        SpellInfo const* auraSpell = m_spellInfo->GetFirstRankSpell();
        if (uint32(target->getLevel() + 10) >= auraSpell->SpellLevel)
            targetInfo.scaleAura = true;
    }

    // Calculate hit result
    if (m_originalCaster)
    {
        targetInfo.missCondition = m_originalCaster->SpellHitResult(target, m_spellInfo, m_canReflect);
        if (m_skipCheck && targetInfo.missCondition != SPELL_MISS_IMMUNE)
            targetInfo.missCondition = SPELL_MISS_NONE;
    }
    else
        targetInfo.missCondition = SPELL_MISS_EVADE; //SPELL_MISS_NONE;

    // Spell have speed - need calculate incoming time
    // Incoming time is zero for self casts. At least I think so.
    if (m_spellInfo->Speed > 0.0f && m_caster != target)
    {
        // calculate spell incoming interval
        /// @todo this is a hack
        float dist = m_caster->GetDistance(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());

        if (dist < 5.0f)
            dist = 5.0f;
        targetInfo.timeDelay = (uint64)std::floor(dist / m_spellInfo->Speed * 1000.0f);

        // Calculate minimum incoming time
        if (m_delayMoment == 0 || m_delayMoment > targetInfo.timeDelay)
            m_delayMoment = targetInfo.timeDelay;
    }
    else if (m_spellInfo->IsAffectedBySpellDelay(m_caster, target))
    {
        targetInfo.timeDelay = m_spellInfo->Delay;
        if (m_delayMoment == 0 || m_delayMoment > targetInfo.timeDelay)
            m_delayMoment = targetInfo.timeDelay;
    }
    else
        targetInfo.timeDelay = 0LL;

    if (targetInfo.timeDelay)
    {
        Player* playerCaster = m_caster->GetCharmerOrOwnerPlayerOrPlayerItself();
        Player* playerTarget = target->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (playerCaster && playerTarget && playerCaster->duel && playerCaster->duel->opponent == playerTarget)
            delayedDuelCheck = true;
    }

    // If target reflect spell back to caster
    if (targetInfo.missCondition == SPELL_MISS_REFLECT)
    {
        // Calculate reflected spell result on caster
        targetInfo.reflectResult = m_caster->SpellHitResult(m_caster, m_spellInfo, m_canReflect);

        if (targetInfo.reflectResult == SPELL_MISS_REFLECT)     // Impossible reflect again, so simply deflect spell
            targetInfo.reflectResult = SPELL_MISS_PARRY;

        // Increase time interval for reflected spells by 1.5
        targetInfo.timeDelay += targetInfo.timeDelay >> 1;
    }
    else
        targetInfo.reflectResult = SPELL_MISS_NONE;

    // Add target to list
    m_UniqueTargetInfo.push_back(targetInfo);
}

void Spell::AddGOTarget(GameObject* go, uint32 effectMask)
{
    for (uint32 effIndex = 0; effIndex < MAX_SPELL_EFFECTS; ++effIndex)
    {
        if (!m_spellInfo->Effects[effIndex].IsEffect())
            effectMask &= ~(1 << effIndex);
        else
        {
            switch (m_spellInfo->Effects[effIndex].Effect)
            {
            case SPELL_EFFECT_GAMEOBJECT_DAMAGE:
            case SPELL_EFFECT_GAMEOBJECT_REPAIR:
            case SPELL_EFFECT_GAMEOBJECT_SET_DESTRUCTION_STATE:
                if (go->GetGoType() != GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING)
                    effectMask &= ~(1 << effIndex);
                break;
            default:
                break;
            }
        }
    }

    if (!effectMask)
        return;

    ObjectGuid targetGUID = go->GetGUID();

    // Lookup target in already in list
    for (std::list<GOTargetInfo>::iterator ihit = m_UniqueGOTargetInfo.begin(); ihit != m_UniqueGOTargetInfo.end(); ++ihit)
    {
        if (targetGUID == ihit->targetGUID)                 // Found in list
        {
            ihit->effectMask |= effectMask;                 // Add only effect mask
            return;
        }
    }

    // This is new target calculate data for him

    GOTargetInfo target;
    target.targetGUID = targetGUID;
    target.effectMask = effectMask;
    target.processed  = false;                              // Effects not apply on target

    // Spell have speed - need calculate incoming time
    if (m_spellInfo->Speed > 0.0f)
    {
        // calculate spell incoming interval
        float dist = m_caster->GetDistance(go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
        if (dist < 5.0f)
            dist = 5.0f;
        target.timeDelay = uint64(floor(dist / m_spellInfo->Speed * 1000.0f));
        if (m_delayMoment == 0 || m_delayMoment > target.timeDelay)
            m_delayMoment = target.timeDelay;
    }
    else
        target.timeDelay = 0LL;

    // Add target to list
    m_UniqueGOTargetInfo.push_back(target);
}

void Spell::AddItemTarget(Item* item, uint32 effectMask)
{
    for (uint32 effIndex = 0; effIndex < MAX_SPELL_EFFECTS; ++effIndex)
        if (!m_spellInfo->Effects[effIndex].IsEffect())
            effectMask &= ~(1 << effIndex);

    // no effects left
    if (!effectMask)
        return;

    // Lookup target in already in list
    for (std::list<ItemTargetInfo>::iterator ihit = m_UniqueItemInfo.begin(); ihit != m_UniqueItemInfo.end(); ++ihit)
    {
        if (item == ihit->item)                            // Found in list
        {
            ihit->effectMask |= effectMask;                 // Add only effect mask
            return;
        }
    }

    // This is new target add data

    ItemTargetInfo target;
    target.item       = item;
    target.effectMask = effectMask;

    m_UniqueItemInfo.push_back(target);
}

void Spell::AddDestTarget(SpellDestination const& dest, uint32 effIndex)
{
    m_destTargets[effIndex] = dest;
}

void Spell::DoAllEffectOnTarget(TargetInfo* target)
{
    if (!target || target->processed)
        return;

    target->processed = true;                               // Target checked in apply effects procedure

    // Get mask of effects for target
    uint8 mask = target->effectMask;

    Unit* unit = m_caster->GetGUID() == target->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, target->targetGUID);
    if (!unit && target->targetGUID.IsPlayer()) // only players may be targeted across maps
    {
        uint8 farMask = 0;
        // create far target mask
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (m_spellInfo->Effects[i].IsFarUnitTargetEffect())
                if ((1 << i) & mask)
                    farMask |= (1 << i);

        if (!farMask)
            return;
        // find unit in world
        unit = ObjectAccessor::FindPlayer(target->targetGUID);
        if (!unit)
            return;

        // do far effects on the unit
        // can't use default call because of threading, do stuff as fast as possible
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (farMask & (1 << i))
                HandleEffects(unit, NULL, NULL, i, SPELL_EFFECT_HANDLE_HIT_TARGET);
        return;
    }

    if (!unit)
    {
        TC_LOG_ERROR("spells", "Spell::DoAllEffectOnTarget Spell (entry: %d) effect applied without valid target on map targetGuid: " UI64FMTD, m_spellInfo->Id, target->targetGUID.GetRawValue());
        return;
    }

    if (unit->IsAlive() != target->alive)
        return;

    if (getState() == SPELL_STATE_DELAYED && !m_spellInfo->IsPositive() && (game::time::GetGameTimeMS() - target->timeDelay) <= unit->m_lastSanctuaryTime)
        return;                                             // No missinfo in that case

    // Get original caster (if exist) and calculate damage/healing from him data
    Unit* caster = m_originalCaster ? m_originalCaster : m_caster;

    // Skip if m_originalCaster not avaiable
    if (!caster)
        return;

    SpellMissInfo missInfo = target->missCondition;

    // Need init unitTarget by default unit (can changed in code on reflect)
    // Or on missInfo != SPELL_MISS_NONE unitTarget undefined (but need in trigger subsystem)
    unitTarget = unit;

    // Reset damage/healing counter
    m_damage = target->damage;
    m_healing = -target->damage;
    m_resist = 0;

    // Fill base trigger info
    uint32 procAttacker = m_procAttacker;
    uint32 procVictim   = m_procVictim;
    uint32 procEx = m_procEx;

    m_spellAura = NULL; // Set aura to null for every target-make sure that pointer is not used for unit without aura applied

                            //Spells with this flag cannot trigger if effect is cast on self
    bool canEffectTrigger = !m_spellInfo->HasAttribute(SPELL_ATTR3_CANT_TRIGGER_PROC) && unitTarget->CanProc() && (CanExecuteTriggersOnHit(mask) || missInfo == SPELL_MISS_IMMUNE || missInfo == SPELL_MISS_IMMUNE2);
    Unit* spellHitTarget = NULL;

    if (missInfo == SPELL_MISS_NONE)                          // In case spell hit target, do all effect on that target
        spellHitTarget = unit;
    else if (missInfo == SPELL_MISS_REFLECT)                // In case spell reflect from target, do all effect on caster (if hit)
    {
        if (target->reflectResult == SPELL_MISS_NONE)       // If reflected spell hit caster -> do all effect on him
        {
            spellHitTarget = m_caster;
            if (m_caster->GetTypeId() == TYPEID_UNIT)
                m_caster->ToCreature()->LowerPlayerDamageReq(target->damage);
        }
    }

    bool enablePvP = false; // need to check PvP state before spell effects, but act on it afterwards

    if (spellHitTarget)
    {
        // if target is flagged for pvp also flag caster if a player
        if (unit->IsPvP() && unit != m_caster)
            if (Player* playercaster = m_caster->ToPlayer())
                if (!playercaster->IsPvP() || playercaster->pvpInfo.EndTimer)
                    enablePvP = true; // Decide on PvP flagging now, but act on it later.
            
        SpellMissInfo missInfo2 = DoSpellHitOnUnit(spellHitTarget, mask, target->scaleAura);
        if (missInfo2 != SPELL_MISS_NONE)
        {
            if (missInfo2 != SPELL_MISS_MISS)
                m_caster->SendSpellMiss(unit, m_spellInfo->Id, missInfo2);
            m_damage = 0;
            spellHitTarget = NULL;
        }
    }

    // Do not take combo points on dodge and miss
    if (missInfo != SPELL_MISS_NONE && m_needComboPoints &&
            m_targets.GetUnitTargetGUID() == target->targetGUID)
    {
        m_needComboPoints = false;
        // Restore spell mods for a miss/dodge/parry Cold Blood
        /// @todo check how broad this rule should be
        if (m_caster->GetTypeId() == TYPEID_PLAYER && (missInfo == SPELL_MISS_MISS ||
                missInfo == SPELL_MISS_DODGE || missInfo == SPELL_MISS_PARRY))
                    RestoreUsedModAuras(14177);
    }

    // Trigger info was not filled in spell::preparedatafortriggersystem - we do it now
    if (canEffectTrigger && !procAttacker && !procVictim)
    {
        m_spellInfo->GetPosititvityProccFlags(procAttacker, procVictim, m_damage, m_healing);
    }
    CallScriptOnHitHandlers();

    // All calculated do it!
    // Do healing and triggers
    if (m_healing > 0)
    {
        uint32 addhealth = m_healing;
        if (target->crit)
        {
            procEx |= PROC_EX_CRITICAL_HIT;
            addhealth = caster->SpellCriticalHealingBonus(m_spellInfo, addhealth, NULL);
        }
        else
            procEx |= PROC_EX_NORMAL_HIT;

        // AI: Call BeforeSpellHit(...) for healing
        if (spellHitTarget)
            if (spellHitTarget->GetTypeId() == TYPEID_UNIT)
                if (spellHitTarget->ToCreature()->IsAIEnabled)
                {
                    int32 negativeAddHealth = - (int32)addhealth;
                    spellHitTarget->ToCreature()->AI()->BeforeSpellHit(m_caster, m_spellInfo, negativeAddHealth);
                    addhealth = -negativeAddHealth;
                }



        int32 gain = caster->HealBySpell(unitTarget, m_spellInfo, addhealth, target->crit);
        unitTarget->GetThreatManager().ForwardThreatForAssistingMe(caster, float(gain) * 0.5f, m_spellInfo);
        m_healing = gain;

        // Do triggers for unit (reflect triggers passed on hit phase for correct drop charge)
        if (canEffectTrigger && missInfo != SPELL_MISS_REFLECT)
            caster->ProcDamageAndSpell(unitTarget, procAttacker, procVictim, procEx, addhealth, 0, m_attackType, m_spellInfo, m_triggeredByAuraSpell);
    }
    // Do damage and triggers
    else if (m_damage > 0)
    {
        // Fill base damage struct (unitTarget - is real spell target)
        SpellNonMeleeDamage damageInfo(caster, unitTarget, m_spellInfo->Id, m_spellSchoolMask);

        // Add bonuses and fill damageInfo struct
        caster->CalculateSpellDamageTaken(&damageInfo, m_damage, m_spellInfo, m_attackType, target->crit);
        caster->DealDamageMods(damageInfo.target, damageInfo.damage, &damageInfo.absorb);

        if (m_caster->GetTypeId() == TYPEID_PLAYER)
        {
            uint32 targetAmount = m_UniqueTargetInfo.size();
            if (targetAmount > 10)
                damageInfo.damage = damageInfo.damage * 10 / targetAmount;
        }

        // AI: Call BeforeSpellHit(...) for damage
        if (spellHitTarget)
            if (spellHitTarget->GetTypeId() == TYPEID_UNIT)
                if (spellHitTarget->ToCreature()->IsAIEnabled)
                {
                    m_damage = damageInfo.damage;
                    spellHitTarget->ToCreature()->AI()->BeforeSpellHit(m_caster, m_spellInfo, m_damage);
                    damageInfo.damage = m_damage;
                }

        // Send log damage message to client
        caster->SendSpellNonMeleeDamageLog(&damageInfo);

        procEx |= createProcExtendMask(&damageInfo, missInfo);
        procVictim |= PROC_FLAG_TAKEN_DAMAGE;

        // Do triggers for unit (reflect triggers passed on hit phase for correct drop charge)
        if (canEffectTrigger && missInfo != SPELL_MISS_REFLECT)
        {
            caster->ProcDamageAndSpell(unitTarget, procAttacker, procVictim, procEx, damageInfo.damage, damageInfo.absorb, m_attackType, m_spellInfo, m_triggeredByAuraSpell);
			if (caster->GetTypeId() == TYPEID_PLAYER && m_spellInfo->HasAttribute(SPELL_ATTR0_STOP_ATTACK_TARGET) == 0 &&
				(m_spellInfo->DmgClass == SPELL_DAMAGE_CLASS_MELEE || m_spellInfo->DmgClass == SPELL_DAMAGE_CLASS_RANGED))
				caster->ToPlayer()->CastItemCombatSpell(unitTarget, m_attackType, this->GetSpellInfo(), procAttacker, procVictim, procEx);
        }

        /* process anticheat check */
        if (caster->GetTypeId() == TYPEID_PLAYER)
            ((Player*)caster)->GetAntiCheat()->DoAntiCheatCheck(CHECK_DAMAGE_SPELL, m_spellInfo->Id, 0, damageInfo.damage);


        m_damage = damageInfo.damage;

        caster->DealSpellDamage(&damageInfo, true);
    }
    // Passive spell hits/misses or active spells only misses (only triggers)
    else
    {
        // Fill base damage struct (unitTarget - is real spell target)
        SpellNonMeleeDamage damageInfo(caster, unitTarget, m_spellInfo->Id, m_spellSchoolMask);
        procEx |= createProcExtendMask(&damageInfo, missInfo);
        // Do triggers for unit (reflect triggers passed on hit phase for correct drop charge)
        if (canEffectTrigger && missInfo != SPELL_MISS_REFLECT)
            caster->ProcDamageAndSpell(unit, procAttacker, procVictim, procEx, 0, 0, m_attackType, m_spellInfo, m_triggeredByAuraSpell);

        // Failed Pickpocket, reveal rogue
        if (missInfo == SPELL_MISS_RESIST && m_spellInfo->HasAttribute(SPELL_ATTR0_CU_PICKPOCKET) && unitTarget->GetTypeId() == TYPEID_UNIT)
        {
            m_caster->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TALK);
            unitTarget->ToCreature()->EngageWithTarget(m_caster);
        }
    }

    if (missInfo != SPELL_MISS_EVADE && m_caster->IsValidAttackTarget(unit) && (!m_spellInfo->IsPositive() || m_spellInfo->HasEffect(SPELL_EFFECT_DISPEL)))
    {
        if (!m_spellInfo->HasAttribute(SPELL_ATTR1_NO_THREAT))
            m_caster->CombatStart(unit, !m_spellInfo->HasAttribute(SPELL_ATTR3_NO_INITIAL_AGGRO));

        if (!unit->IsStandState())
            unit->SetStandState(UNIT_STAND_STATE_STAND);
    }

    // Check for SPELL_ATTR7_INTERRUPT_ONLY_NONPLAYER
    if (m_spellInfo->HasAttribute(SPELL_ATTR7_INTERRUPT_ONLY_NONPLAYER) && unit->GetTypeId() != TYPEID_PLAYER)
        caster->CastSpell(unit, 32747, true);

    if (spellHitTarget)
    {
        *spellHitTarget <<= Fire::HitBySpell(m_spellInfo->Id).Bind(this);
        *this <<= Fire::HitTarget.Bind(spellHitTarget);

        //AI functions
        if (spellHitTarget->GetTypeId() == TYPEID_UNIT)
            if (spellHitTarget->ToCreature()->IsAIEnabled)
                spellHitTarget->ToCreature()->AI()->SpellHit(m_caster, m_spellInfo);

        if (m_caster->GetTypeId() == TYPEID_UNIT && m_caster->ToCreature()->IsAIEnabled)
            m_caster->ToCreature()->AI()->SpellHitTarget(spellHitTarget, m_spellInfo);

        // Needs to be called after dealing damage/healing to not remove breaking on damage auras
        DoTriggersOnSpellHit(spellHitTarget, mask);

        if (unit != m_caster && enablePvP)
            m_caster->ToPlayer()->UpdatePvP(true);

        CallScriptAfterHitHandlers();
    }
}

SpellMissInfo Spell::DoSpellHitOnUnit(Unit* unit, uint32 effectMask, bool scaleAura)
{
    if (!unit || !effectMask)
        return SPELL_MISS_EVADE;

    // For delayed spells immunity may be applied between missile launch and hit - check immunity for that case
    if (m_spellInfo->Speed && (unit->IsImmunedToDamage(m_spellInfo) || unit->IsImmunedToSpell(m_spellInfo)))
        return SPELL_MISS_IMMUNE;

    // disable effects to which unit is immune
    SpellMissInfo returnVal = SPELL_MISS_IMMUNE;
    for (uint32 effectNumber = 0; effectNumber < MAX_SPELL_EFFECTS; ++effectNumber)
    {
        if (effectMask & (1 << effectNumber))
        {
            if (unit->IsImmunedToSpellEffect(m_spellInfo, effectNumber))
                effectMask &= ~(1 << effectNumber);
            else if (m_spellInfo->Effects[effectNumber].IsAura() && !m_spellInfo->IsPositiveEffect(effectNumber))
            {
                int32 debuff_resist_chance = unit->GetMaxPositiveAuraModifierByMiscValue(SPELL_AURA_MOD_DEBUFF_RESISTANCE, int32(m_spellInfo->Dispel), true);
                debuff_resist_chance += unit->GetMaxNegativeAuraModifierByMiscValue(SPELL_AURA_MOD_DEBUFF_RESISTANCE, int32(m_spellInfo->Dispel));

                if (debuff_resist_chance > 0)
                    if (irand(0, 10000) <= (debuff_resist_chance * 100))
                    {
                        effectMask &= ~(1 << effectNumber);
                        returnVal = SPELL_MISS_RESIST;
                    }
            }
        }
    }

    if (!effectMask)
        return returnVal;

    PrepareScriptHitHandlers();
    CallScriptBeforeHitHandlers();

    if (Player* player = unit->ToPlayer())
    {
        player->StartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_SPELL_TARGET, m_spellInfo->Id);
        player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET, m_spellInfo->Id, 0, m_caster);
        player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BE_SPELL_TARGET2, m_spellInfo->Id);
    }

    if (Player* player = m_caster->ToPlayer())
    {
        player->StartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_SPELL_CASTER, m_spellInfo->Id);
        player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL2, m_spellInfo->Id, 0, unit);
    }

    if (m_caster != unit)
    {
        // Recheck UNIT_FLAG_NON_ATTACKABLE for delayed spells in case of negative spell
        if (m_spellInfo->Speed > 0.0f && unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE) && !m_spellInfo->_IsPositiveSpell() && unit->GetCharmerOrOwnerGUID() != m_caster->GetGUID())
            return SPELL_MISS_EVADE;

        if (m_caster->_IsValidAttackTarget(unit, m_spellInfo))
        {
            // Distract should not remove stealth
            if (m_spellInfo->Id != 1725)
                unit->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_HITBYSPELL);
            if (!m_spellInfo->HasAttribute(SPELL_ATTR0_CU_DONT_BREAK_STEALTH) && !m_spellInfo->IsTargetingArea())
                unit->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);

            if (m_spellInfo->HasAttribute(SPELL_ATTR0_CU_ALWAYS_BREAK_STEALTH))
                unit->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);

            if (m_spellInfo->IsAffectedByBinaryResistance(m_originalCaster ? m_originalCaster : m_caster, unit))
            {
                m_resist = m_caster->CalcSpellResistance(unit, m_spellSchoolMask , m_spellInfo);
                if (m_resist >= 100)
                    return SPELL_MISS_RESIST;
            }
        }
        else if (m_caster->IsFriendlyTo(unit))
        {
            // for delayed spells ignore negative spells (after duel end) for friendly targets
            if (delayedDuelCheck)
                return SPELL_MISS_EVADE;

            // assisting case, healing and resurrection
            if (unit->HasUnitState(UNIT_STATE_ATTACK_PLAYER))
            {
                if (Player* playerOwner = m_caster->GetCharmerOrOwnerPlayerOrPlayerItself())
                {
                    playerOwner->SetContestedPvP();
                    playerOwner->UpdatePvP(true);
                }
            }
            if (unit->IsInCombat() && !m_spellInfo->HasAttribute(SPELL_ATTR3_NO_INITIAL_AGGRO))
            {
                m_caster->SetInCombatState(unit->GetCombatTimer() > 0, unit);
                unit->GetThreatManager().ForwardThreatForAssistingMe(m_caster, 0.0f, nullptr, true);
            }
        }
    }

    else if (m_spellInfo->IsAffectedByBinaryResistance(m_originalCaster ? m_originalCaster : m_caster, unit) && !m_spellInfo->IsPositive())
    {
        m_resist = m_caster->CalcSpellResistance(unit, m_spellSchoolMask, m_spellInfo);
        if (m_resist >= 100)
            return SPELL_MISS_RESIST;
    }

    uint8 aura_effmask = 0;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        if (effectMask & (1 << i) && m_spellInfo->Effects[i].IsUnitOwnedAuraEffect())
            aura_effmask |= 1 << i;

    // Get Data Needed for Diminishing Returns, some effects may have multiple auras, so this must be done on spell hit, not aura add
    m_diminishGroup = GetDiminishingReturnsGroupForSpell(m_spellInfo, m_triggeredByAuraSpell != nullptr);
    if (m_diminishGroup && aura_effmask)
    {
        m_diminishLevel = unit->GetDiminishing(m_diminishGroup);
        DiminishingReturnsType type = GetDiminishingReturnsGroupType(m_diminishGroup);
        // Increase Diminishing on unit, current informations for actually casts will use values above
        if ((type == DRTYPE_PLAYER &&
            (unit->GetCharmerOrOwnerPlayerOrPlayerItself() || (unit->GetTypeId() == TYPEID_UNIT && unit->ToCreature()->GetCreatureTemplate()->flags_extra & CREATURE_FLAG_EXTRA_ALL_DIMINISH))) ||
            type == DRTYPE_ALL)
            unit->IncrDiminishing(m_diminishGroup);
    }

    if (aura_effmask)
    {
        // Select rank for aura with level requirements only in specific cases
        // Unit has to be target only of aura effect, both caster and target have to be players, target has to be other than unit target
        SpellInfo const* aurSpellInfo = m_spellInfo;
        int32 basePoints[3];
        if (scaleAura)
        {
            aurSpellInfo = m_spellInfo->GetAuraRankForLevel(unitTarget->getLevel());
            ASSERT(aurSpellInfo);
            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                basePoints[i] = aurSpellInfo->Effects[i].BasePoints;
                if (m_spellInfo->Effects[i].Effect != aurSpellInfo->Effects[i].Effect)
                {
                    aurSpellInfo = m_spellInfo;
                    break;
                }
            }
        }

        if (m_originalCaster)
        {
            bool refresh = false;
            m_spellAura = Aura::TryRefreshStackOrCreate(aurSpellInfo, effectMask, unit,
                m_originalCaster, (aurSpellInfo == m_spellInfo)? &m_spellValue->EffectBasePoints[0] : &basePoints[0], m_CastItem, ObjectGuid::Empty, &refresh, m_softRefresh);
            if (m_spellAura)
            {
                // Set aura stack amount to desired value
                if (m_spellValue->AuraStackAmount > 1)
                {
                    if (!refresh)
                        m_spellAura->SetStackAmount(m_spellValue->AuraStackAmount);
                    else
                        m_spellAura->ModStackAmount(m_spellValue->AuraStackAmount);
                }

                // Now Reduce spell duration using data received at spell hit
                int32 duration = m_spellAura->GetMaxDuration();
                int32 limitduration = GetDiminishingReturnsLimitDuration(m_diminishGroup, aurSpellInfo);
                float diminishMod = unit->ApplyDiminishingToDuration(m_diminishGroup, duration, m_originalCaster, m_diminishLevel, limitduration);

                // unit is immune to aura if it was diminished to 0 duration
                if (diminishMod == 0.0f)
                {
                    m_spellAura->Remove();
                    bool found = false;
                    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                        if (effectMask & (1 << i) && m_spellInfo->Effects[i].Effect != SPELL_EFFECT_APPLY_AURA)
                            found = true;
                    if (!found)
                        return SPELL_MISS_IMMUNE;
                }
                else
                {
                    ((UnitAura*)m_spellAura)->SetDiminishGroup(m_diminishGroup);

                    bool positive = m_spellAura->GetSpellInfo()->IsPositive();
                    if (AuraApplication* aurApp = m_spellAura->GetApplicationOfTarget(m_originalCaster->GetGUID()))
                        positive = aurApp->IsPositive();

                    duration = m_originalCaster->ModSpellDuration(aurSpellInfo, unit, duration, positive, effectMask);

                    // Haste modifies duration of channeled spells
                    if (m_spellInfo->IsChanneled())
                    {
                        if (m_spellInfo->HasAttribute(SPELL_ATTR5_HASTE_AFFECT_DURATION))
                            m_originalCaster->ModSpellCastTime(aurSpellInfo, duration, this);

                        //Spell Fix Improved Succubuss Duration
                        if (m_spellInfo->Id == 6358 && unit->GetTypeId() == TYPEID_UNIT && m_originalCaster->GetOwner())
                        {
                            float mod = 1.0f;
                            float durationadd = 0.0f;
                            if (m_originalCaster->GetOwner()->HasAura(18754))
                                durationadd += float(1.5*IN_MILLISECONDS*0.22);
                            else if (m_originalCaster->GetOwner()->HasAura(18755))
                                durationadd += float(1.5*IN_MILLISECONDS*0.44);
                            else if (m_originalCaster->GetOwner()->HasAura(18756))
                                durationadd += float(1.5*IN_MILLISECONDS*0.66);

                            if (durationadd)
                            {
                                switch (m_diminishLevel)
                                {
                                    case DIMINISHING_LEVEL_1: break;
                                    // lol, we lost 1 second here
                                    case DIMINISHING_LEVEL_2: duration += 1000; mod = 0.5f; break;
                                    case DIMINISHING_LEVEL_3: duration += 1000; mod = 0.25f; break;
                                    case DIMINISHING_LEVEL_IMMUNE: { m_spellAura->Remove(); return SPELL_MISS_IMMUNE; }
                                    default: break;
                                }
                                durationadd *= mod;
                                duration += int32(durationadd);
                            }
                        }
                    }
                    else if (m_originalCaster->HasAuraTypeWithAffectMask(SPELL_AURA_PERIODIC_HASTE, aurSpellInfo) || m_spellInfo->HasAttribute(SPELL_ATTR5_HASTE_AFFECT_DURATION))
                        duration = int32(duration * m_originalCaster->GetFloatValue(UNIT_MOD_CAST_SPEED));

                    if (duration != m_spellAura->GetMaxDuration())
                    {
                        m_spellAura->SetMaxDuration(duration);
                        m_spellAura->SetDuration(duration);
                    }
                    m_spellAura->_RegisterForTargets();
                }
            }
        }
    }

    for (uint32 effectNumber = 0; effectNumber < MAX_SPELL_EFFECTS; ++effectNumber)
        if (effectMask & (1 << effectNumber))
            HandleEffects(unit, NULL, NULL, effectNumber, SPELL_EFFECT_HANDLE_HIT_TARGET);

    return SPELL_MISS_NONE;
}

void Spell::DoTriggersOnSpellHit(Unit* unit, uint8 effMask)
{
    // Apply additional spell effects to target
    /// @todo move this code to scripts
    if (m_preCastSpell)
    {
        // Paladin immunity shields
        if (m_preCastSpell == 61988)
        {
            // Cast Forbearance
            m_caster->CastSpell(unit, 25771, true);
            // Cast Avenging Wrath Marker
            unit->CastSpell(unit, 61987, true);
        }

        // Avenging Wrath
        if (m_preCastSpell == 61987)
            // Cast the serverside immunity shield marker
            m_caster->CastSpell(unit, 61988, true);

        if (sSpellMgr->GetSpellInfo(m_preCastSpell))
            // Blizz seems to just apply aura without bothering to cast
            m_caster->AddAura(m_preCastSpell, unit);
    }

    // handle SPELL_AURA_ADD_TARGET_TRIGGER auras
    // this is executed after spell proc spells on target hit
    // spells are triggered for each hit spell target
    // info confirmed with retail sniffs of permafrost and shadow weaving
    if (!m_hitTriggerSpells.empty())
    {
        for (const Spell::HitTriggerSpell hit : m_hitTriggerSpells)
        {
            if (CanExecuteTriggersOnHit(effMask, hit.triggeredByAura) && roll_chance_i(hit.chance))
            {
                // we store the duration, if there is already an application of the triggered spell
                // used to fix up duration for multiple sources of the same triggered spell
                // so that ie. Deepwounds (6s) won't override Bloody Frenzy from Rend (12s)
                int32 oldDuration = -1;
                if (Aura* aura = unit->GetAura(hit.triggeredSpell->Id))
                    oldDuration = aura->GetDuration();

                Item* item = nullptr;
                if (Player* player = m_caster->To<Player>())
                    item = player->GetItemByGuid(hit.triggeredByItemGUID);
                m_caster->CastSpell(unit, hit.triggeredSpell, true, item);
                TC_LOG_DEBUG("spells", "Spell %d triggered spell %d by SPELL_AURA_ADD_TARGET_TRIGGER aura", m_spellInfo->Id, hit.triggeredSpell->Id);

                // SPELL_AURA_ADD_TARGET_TRIGGER auras shouldn't trigger auras without duration
                // set duration of current aura to the triggered spell
                if (hit.triggeredSpell->GetDuration() == -1)
                {
                    if (Aura* triggeredAur = unit->GetAura(hit.triggeredSpell->Id, m_caster->GetGUID()))
                    {
                        // get duration from aura-only once
                        Aura* aur = unit->GetAura(m_spellInfo->Id, m_caster->GetGUID());
                        int32 _duration = aur ? aur->GetDuration() : -1;

                        // longer duration wins - so aura always expires with the last active source
                        _duration = std::max<int32>(_duration, oldDuration);

                        triggeredAur->SetDuration(_duration);
                    }
                }
            }
        }
    }

    // trigger linked auras remove/apply
    /// @todo remove/cleanup this, as this table is not documented and people are doing stupid things with it
    if (std::vector<int32> const* spellTriggered = sSpellMgr->GetSpellLinked(m_spellInfo->Id + SPELL_LINK_HIT))
    {
        for (std::vector<int32>::const_iterator i = spellTriggered->begin(); i != spellTriggered->end(); ++i)
        {
            if (*i < 0)
                unit->RemoveAurasDueToSpell(-(*i));
            else
                unit->CastSpell(unit, *i, true, nullptr, nullptr, m_caster->GetGUID());
        }
    }
}

void Spell::DoAllEffectOnTarget(GOTargetInfo* target)
{
    if (target->processed)                                  // Check target
        return;
    target->processed = true;                               // Target checked in apply effects procedure

    uint32 effectMask = target->effectMask;
    if (!effectMask)
        return;

    GameObject* go = m_caster->GetMap()->GetGameObject(target->targetGUID);
    if (!go)
        return;

    PrepareScriptHitHandlers();
    CallScriptBeforeHitHandlers();

    for (uint32 effectNumber = 0; effectNumber < MAX_SPELL_EFFECTS; ++effectNumber)
        if (effectMask & (1 << effectNumber))
            HandleEffects(NULL, NULL, go, effectNumber, SPELL_EFFECT_HANDLE_HIT_TARGET);

    if (go->AI())
        go->AI()->SpellHit(m_caster, m_spellInfo);

    CallScriptOnHitHandlers();
    CallScriptAfterHitHandlers();
}

void Spell::DoAllEffectOnTarget(ItemTargetInfo* target)
{
    uint32 effectMask = target->effectMask;
    if (!target->item || !effectMask)
        return;

    PrepareScriptHitHandlers();
    CallScriptBeforeHitHandlers();

    for (uint32 effectNumber = 0; effectNumber < MAX_SPELL_EFFECTS; ++effectNumber)
        if (effectMask & (1 << effectNumber))
            HandleEffects(NULL, target->item, NULL, effectNumber, SPELL_EFFECT_HANDLE_HIT_TARGET);

    CallScriptOnHitHandlers();

    CallScriptAfterHitHandlers();
}

bool Spell::UpdateChanneledTargetList()
{
    // Not need check return true
    if (m_channelTargetEffectMask == 0)
        return true;

    uint8 channelTargetEffectMask = m_channelTargetEffectMask;
    uint8 channelAuraMask = 0;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        if (m_spellInfo->Effects[i].Effect == SPELL_EFFECT_APPLY_AURA)
            channelAuraMask |= 1<<i;

    channelAuraMask &= channelTargetEffectMask;

    float range = 0;
    if (channelAuraMask)
    {
        range = m_spellInfo->GetMaxRange(m_spellInfo->IsPositive());
        if (Player* modOwner = m_caster->GetSpellModOwner())
            modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_RANGE, range, this);
        
        // Blizzard adds some extra range after channeling start.
        range += 5.f;
    }

    for (std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if (ihit->missCondition == SPELL_MISS_NONE && (channelTargetEffectMask & ihit->effectMask))
        {
            Unit* unit = m_caster->GetGUID() == ihit->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID);

            if (!unit)
                continue;

            if (IsValidDeadOrAliveTarget(unit))
            {
                if (channelAuraMask & ihit->effectMask)
                {
                    if (AuraApplication * aurApp = unit->GetAuraApplication(m_spellInfo->Id, m_originalCasterGUID))
                    {
                        if (m_caster != unit && !m_caster->IsWithinDistInMap(unit, range))
                        {
                            ihit->effectMask &= ~aurApp->GetEffectMask();
                            unit->RemoveAura(aurApp);
                            continue;
                        }
                    }
                    else // aura is dispelled
                        continue;
                }

                channelTargetEffectMask &= ~ihit->effectMask;   // remove from need alive mask effect that have alive target
            }
        }
    }

    // is all effects from m_needAliveTargetMask have alive targets
    return channelTargetEffectMask == 0;
}

void Spell::prepare(SpellCastTargets const* targets, AuraEffect const* triggeredByAura)
{
    *m_caster <<= Fire::SpellCast(GetSpellInfo()->Id).Bind(this);

    if (m_CastItem)
    {
        m_castItemGUID = m_CastItem->GetGUID();
        m_castItemEntry = m_CastItem->GetEntry();
    }
    else
    {
        m_castItemGUID.Clear();
        m_castItemEntry = 0;
    }

    InitExplicitTargets(*targets);

    // Fill aura scaling information
    if (m_caster->IsControlledByPlayer() && !m_spellInfo->IsPassive() && m_spellInfo->SpellLevel && !m_spellInfo->IsChanneled() && !(_triggeredCastFlags & TRIGGERED_IGNORE_AURA_SCALING))
    {
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (m_spellInfo->Effects[i].Effect == SPELL_EFFECT_APPLY_AURA)
            {
                // Change aura with ranks only if basepoints are taken from spellInfo and aura is positive
                if (m_spellInfo->IsPositiveEffect(i))
                {
                    m_auraScaleMask |= (1 << i);
                    if (m_spellValue->EffectBasePoints[i] != m_spellInfo->Effects[i].BasePoints)
                    {
                        m_auraScaleMask = 0;
                        break;
                    }
                }
            }
        }
    }

    m_spellState = SPELL_STATE_PREPARING;

    if (triggeredByAura)
        m_triggeredByAuraSpell  = triggeredByAura->GetSpellInfo();

    // create and add update event for this spell
    SpellEvent* Event = new SpellEvent(this);
    m_caster->m_Events.AddEvent(Event, m_caster->m_Events.CalculateTime(1));

    // repeatable spells should not be skipped by current channel
    const bool isAutoRepeat = m_spellInfo->HasAttribute(SPELL_ATTR2_AUTOREPEAT_FLAG);

    //Prevent casting at cast another spell (ServerSide check)
    if (!isAutoRepeat && !(_triggeredCastFlags & TRIGGERED_IGNORE_CAST_IN_PROGRESS) && m_caster->IsNonMeleeSpellCast(false, true, true) && m_cast_count)
    {
        SendCastResult(SPELL_FAILED_SPELL_IN_PROGRESS);
        finish(false);
        return;
    }

    if (DisableMgr::IsDisabledFor(DISABLE_TYPE_SPELL, m_spellInfo->Id, m_caster))
    {
        SendCastResult(SPELL_FAILED_SPELL_UNAVAILABLE);
        finish(false);
        return;
    }
    LoadScripts();

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
        m_caster->ToPlayer()->SetSpellModTakingSpell(this, true);
    // Fill cost data (do not use power for item casts)
    m_powerCost = m_CastItem ? 0 : m_spellInfo->CalcPowerCost(m_caster, m_spellSchoolMask);
    if (m_caster->GetTypeId() == TYPEID_PLAYER)
        m_caster->ToPlayer()->SetSpellModTakingSpell(this, false);

    // Set combo point requirement
    if ((_triggeredCastFlags & TRIGGERED_IGNORE_COMBO_POINTS) || m_CastItem || !m_caster->m_movedPlayer)
        m_needComboPoints = false;

    SpellCastResult result = CheckCast(true);
    if (result != SPELL_CAST_OK && !IsAutoRepeat())          //always cast autorepeat dummy for triggering
    {
        // Periodic auras should be interrupted when aura triggers a spell which can't be cast
        // for example bladestorm aura should be removed on disarm as of patch 3.3.5
        // channeled periodic spells should be affected by this (arcane missiles, penance, etc)
        // a possible alternative sollution for those would be validating aura target on unit state change
        if (triggeredByAura && triggeredByAura->IsPeriodic() && !triggeredByAura->GetBase()->IsPassive() && triggeredByAura->GetBase()->IsSingleTarget())
        {
            SendChannelUpdate(0);
            triggeredByAura->GetBase()->SetDuration(0);
        }

        if (m_caster->GetTypeId() == TYPEID_PLAYER)
        {
            RestoreUsedModAuras();
            // cleanup after mod system
            // triggered spell pointer can be not removed in some cases
            m_caster->ToPlayer()->SetSpellModTakingSpell(this, false);
        }

        SendCastResult(result);

        finish(false);
        return;
    }

    //Custom Sheep of Death
    //######
    if (m_caster->GetTypeId() == TYPEID_PLAYER)
        if (m_caster->GetMapId() == 595 && m_caster->GetPhaseMask() == 2) //map of HDZ4
            //cancel all spells in this phase except the 3 spells used by the event
            if (m_spellInfo->Id != 47292        //spectator visual
                && m_spellInfo->Id != 37692     //sheep blood visual
                && m_spellInfo->Id !=100007)    //speed nerf
            {
                SendCastResult(SPELL_FAILED_NOT_HERE);
                finish(false);
                return;
            }
    //######

    // Prepare data for triggers
    prepareDataForTriggerSystem(triggeredByAura);

    if (Player* player = m_caster->ToPlayer())
    {
        if (!player->GetCommandStatus(CHEAT_CASTTIME))
        {
            player->SetSpellModTakingSpell(this, true);
            // calculate cast time (calculated after first CheckCast check to prevent charge counting for first CheckCast fail)
            m_casttime = m_spellInfo->CalcCastTime(this);
            player->SetSpellModTakingSpell(this, false);
        }
        else
            m_casttime = 0; // Set cast time to 0 if .cheat casttime is enabled.
    }
    else
        m_casttime = m_spellInfo->CalcCastTime(this);

    // don't allow channeled spells / spells with cast time to be cast while moving
    // exception are only channeled spells that have no casttime and SPELL_ATTR5_CAN_CHANNEL_WHEN_MOVING
    // (even if they are interrupted on moving, spells with almost immediate effect get to have their effect processed before movement interrupter kicks in)
    if ((m_spellInfo->IsChanneled() || m_casttime) && m_caster->GetTypeId() == TYPEID_PLAYER && (!m_caster->IsCharmed() || !m_caster->GetCharmerGUID().IsCreature()) && m_caster->isMoving() && m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_MOVEMENT)
    {
        // 1. Is a channel spell, 2. Has no casttime, 3. And has flag to allow movement during channel
        if (!(m_spellInfo->IsChanneled() && !m_casttime && m_spellInfo->HasAttribute(SPELL_ATTR5_CAN_CHANNEL_WHEN_MOVING)))
        {
            SendCastResult(SPELL_FAILED_MOVING);
            finish(false);
            return;
        }
    }

    // set timer base at cast time
    ReSetTimer();

    TC_LOG_DEBUG("spells", "Spell::prepare: spell id %u source %u caster %d customCastFlags %u mask %u", m_spellInfo->Id, m_caster->GetEntry(), m_originalCaster ? m_originalCaster->GetEntry() : -1, _triggeredCastFlags, m_targets.GetTargetMask());
    if ((cast_timing_log_ = RG::Logging::SpellTiming::StartLog(m_caster, this)))
        cast_timing_log_->Start(m_caster, this, targets);

    //Containers for channeled spells have to be set
    /// @todoApply this to all cast spells if needed
    // Why check duration? 29350: channelled triggers channelled
    if ((_triggeredCastFlags & TRIGGERED_CAST_DIRECTLY) && (!m_spellInfo->IsChanneled() || !m_spellInfo->GetMaxDuration()))
        cast(true);
    else
    {
        // stealth must be removed at cast starting (at show channel bar)
        // skip triggered spell (item equip spell casting and other not explicit character casts/item uses)
        if (!(_triggeredCastFlags & TRIGGERED_IGNORE_AURA_INTERRUPT_FLAGS) && m_spellInfo->IsBreakingStealth())
        {
            m_caster->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_CAST);
            for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                if (m_spellInfo->Effects[i].GetUsedTargetObjectType() == TARGET_OBJECT_TYPE_UNIT)
                {
                    m_caster->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_SPELL_ATTACK);
                    break;
                }
        }

        m_caster->SetCurrentCastSpell(this);
        SendSpellStart();

        // set target for proper facing
        if ((m_casttime || m_spellInfo->IsChanneled()) && !(_triggeredCastFlags & TRIGGERED_IGNORE_SET_FACING))
            if (m_caster->GetTypeId() == TYPEID_UNIT && m_targets.GetObjectTarget() && m_caster != m_targets.GetObjectTarget())
                m_caster->ToCreature()->FocusTarget(this, m_targets.GetObjectTarget());

        if (!(_triggeredCastFlags & TRIGGERED_IGNORE_GCD))
            TriggerGlobalCooldown();

        if (!m_casttime && !m_spellInfo->StartRecoveryTime && GetCurrentContainer() == CURRENT_GENERIC_SPELL)
            cast(true);

        if (m_casttime)
            if (m_spellInfo->HasAttribute(SPELL_ATTR6_SUSPEND_ATTACK_TIMER))
                m_caster->SetAttackTimerPause(Milliseconds(m_casttime));
    }
}

void Spell::cancel()
{
    if (m_spellState == SPELL_STATE_FINISHED)
        return;

    // Patch 3.3.5 - Bandages and Hearthstone will not be interrupted if caster has aura: Divine Shield
    if ((m_spellInfo->SpellIconID == 776 || m_spellInfo->SpellIconID == 104) && m_caster->HasAura(642) && !m_caster->isMoving())
        return;

    uint32 oldState = m_spellState;
    m_spellState = SPELL_STATE_FINISHED;

    m_autoRepeat = false;
    switch (oldState)
    {
        case SPELL_STATE_PREPARING:
            CancelGlobalCooldown();
            if (m_caster->GetTypeId() == TYPEID_PLAYER)
                RestoreUsedModAuras();
            // no break
        case SPELL_STATE_DELAYED:
            SendInterrupted(0);
            SendCastResult(SPELL_FAILED_INTERRUPTED);
            break;

        case SPELL_STATE_CASTING:
            for (std::list<TargetInfo>::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
                if ((*ihit).missCondition == SPELL_MISS_NONE)
                    if (Unit* unit = m_caster->GetGUID() == ihit->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID))
                        unit->RemoveOwnedAura(m_spellInfo->Id, m_originalCasterGUID, 0, AURA_REMOVE_BY_CANCEL);

            SendChannelUpdate(0);

            // spell is canceled-take mods and clear list
            DropUsedModAurasCharges(true);
            break;

        default:
            break;
    }

    if (m_spellInfo->HasAttribute(SPELL_ATTR6_SUSPEND_ATTACK_TIMER))
    {
        m_caster->SetAttackTimerPause(0s);
        m_caster->UpdateAttackTimer(m_casttime - m_timer);
    }

    SetReferencedFromCurrent(false);
    if (m_selfContainer && *m_selfContainer == this)
        *m_selfContainer = NULL;

    m_caster->RemoveDynObject(m_spellInfo->Id);
    if (m_spellInfo->IsChanneled()) // if not channeled then the object for the current cast wasn't summoned yet
        m_caster->RemoveGameObject(m_spellInfo->Id, true);

    //set state back so finish will be processed
    m_spellState = oldState;

    finish(false);

    if (cast_timing_log_)
        cast_timing_log_->Cancel();
}

void Spell::cast(bool skipCheck)
{
    // update pointers base at GUIDs to prevent access to non-existed already object
    if (!UpdatePointers())
    {
        // cancel the spell if UpdatePointers() returned false, something wrong happened there
        cancel();
        return;
    }

    // cancel at lost explicit target during cast
    if (m_targets.GetObjectTargetGUID() && !m_targets.GetObjectTarget())
    {
        cancel();
        return;
    }

    if (cast_timing_log_)
        cast_timing_log_->Success();

    if (Player* playerCaster = m_caster->ToPlayer())
    {
        // now that we've done the basic check, now run the scripts
        // should be done before the spell is actually executed
        sScriptMgr->OnPlayerSpellCast(playerCaster, this, m_targets, skipCheck);

        // As of 3.0.2 pets begin attacking their owner's target immediately
        // Let any pets know we've attacked something. Check DmgClass for harmful spells only
        // This prevents spells such as Hunter's Mark from triggering pet attack
        if (this->GetSpellInfo()->DmgClass != SPELL_DAMAGE_CLASS_NONE)
            if (Pet* playerPet = playerCaster->GetPet())
                if (playerPet->IsAlive() && playerPet->isControlled() && (m_targets.GetTargetMask() & TARGET_FLAG_UNIT))
                    playerPet->AI()->OwnerAttacked(m_targets.GetUnitTarget());


        if (m_spellInfo->Id == 2764 && !playerCaster->IsInCombat() && m_targets.GetUnitTarget()) // Throw
            m_caster->SetInCombatState(true, m_targets.GetUnitTarget());
    }

    SetExecutedCurrently(true);

    if (!(_triggeredCastFlags & TRIGGERED_IGNORE_SET_FACING))
        if (m_caster->GetTypeId() == TYPEID_UNIT && m_targets.GetObjectTarget() && m_caster != m_targets.GetObjectTarget())
            m_caster->SetInFront(m_targets.GetObjectTarget());

    // Should this be done for original caster?
    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        // Set spell which will drop charges for triggered cast spells
        // if not successfully cast, will be remove in finish(false)
        // spells triggered by other spells (not auras!) let their
        // triggering spell taking the auras
        m_caster->ToPlayer()->SetSpellModTakingSpell(this, true);
    }

    CallScriptBeforeCastHandlers();

    // skip check if done already (for instant cast spells for example)
    if (!skipCheck)
    {
        SpellCastResult castResult = CheckCast(false);
        if (castResult != SPELL_CAST_OK)
        {
            SendCastResult(castResult);
            SendInterrupted(0);
            //restore spell mods
            if (m_caster->GetTypeId() == TYPEID_PLAYER)
            {
                RestoreUsedModAuras();
                // cleanup after mod system
                // triggered spell pointer can be not removed in some cases
                m_caster->ToPlayer()->SetSpellModTakingSpell(this, false);
            }
            finish(false);
            SetExecutedCurrently(false);
            return;
        }

        // additional check after cast bar completes (must not be in CheckCast)
        // if trade not complete then remember it in trade data
        if (m_targets.GetTargetMask() & TARGET_FLAG_TRADE_ITEM)
        {
            if (m_caster->GetTypeId() == TYPEID_PLAYER)
            {
                if (TradeData* my_trade = m_caster->ToPlayer()->GetTradeData())
                {
                    if (!my_trade->IsInAcceptProcess())
                    {
                        // Spell will be cast after completing the trade. Silently ignore at this place
                        my_trade->SetSpell(m_spellInfo->Id, m_CastItem);
                        SendCastResult(SPELL_FAILED_DONT_REPORT);
                        SendInterrupted(0);
                        RestoreUsedModAuras();
                        // cleanup after mod system
                        // triggered spell pointer can be not removed in some cases
                        m_caster->ToPlayer()->SetSpellModTakingSpell(this, false);
                        finish(false);
                        SetExecutedCurrently(false);
                        return;
                    }
                }
            }
        }
    }

    SelectSpellTargets();

    // Spell may be finished after target map check
    if (m_spellState == SPELL_STATE_FINISHED)
    {
        SendInterrupted(0);
        //restore spell mods
        if (m_caster->GetTypeId() == TYPEID_PLAYER)
        {
            RestoreUsedModAuras();
            // cleanup after mod system
            // triggered spell pointer can be not removed in some cases
            m_caster->ToPlayer()->SetSpellModTakingSpell(this, false);
        }
        finish(false);
        SetExecutedCurrently(false);
        return;
    }

    if (m_spellInfo->HasAttribute(SPELL_ATTR1_DISMISS_PET))
        if (Creature* pet = ObjectAccessor::GetCreature(*m_caster, m_caster->GetPetGUID()))
            pet->DespawnOrUnsummon();

    PrepareTriggersExecutedOnHit();

    CallScriptOnCastHandlers();

    // traded items have trade slot instead of guid in m_itemTargetGUID
    // set to real guid to be sent later to the client
    m_targets.UpdateTradeSlotItem();

    if (Player* player = m_caster->ToPlayer())
    {
        if (!(_triggeredCastFlags & TRIGGERED_IGNORE_CAST_ITEM) && m_CastItem)
        {
            player->StartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_ITEM, m_CastItem->GetEntry());
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_USE_ITEM, m_CastItem->GetEntry());
        }

        player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL, m_spellInfo->Id);
    }

    if (!(_triggeredCastFlags & TRIGGERED_IGNORE_POWER_AND_REAGENT_COST))
    {
        // Powers have to be taken before SendSpellGo
        TakePower();
        TakeReagents();                                         // we must remove reagents before HandleEffects to allow place crafted item in same slot
    }
    else if (Item* targetItem = m_targets.GetItemTarget())
    {
        /// Not own traded item (in trader trade slot) req. reagents including triggered spell case
        if (targetItem->GetOwnerGUID() != m_caster->GetGUID())
            TakeReagents();
    }

    // CAST SPELL
    SendSpellCooldown();

    PrepareScriptHitHandlers();

    HandleLaunchPhase();

    // we must send smsg_spell_go packet before m_castItem delete in TakeCastItem()...

    *this <<= Fire::CastStart;

    SendSpellGo();

    // Okay, everything is prepared. Now we need to distinguish between immediate and evented delayed spells
    if (((m_spellInfo->Speed > 0.0f || m_spellInfo->IsAffectedBySpellDelay(m_caster)) && !m_spellInfo->IsChanneled()) || m_spellInfo->HasAttribute(SPELL_ATTR4_UNK4))
    {
        // Remove used for cast item if need (it can be already NULL after TakeReagents call
        // in case delayed spell remove item at cast delay start
        TakeCastItem();
		// need to cache spellmods now because they get removed at the end of cast
		// but we still need to know their values when this spell hits
		CacheSpellModsForDelayedHit();

        // Okay, maps created, now prepare flags
        m_immediateHandled = false;
        m_spellState = SPELL_STATE_DELAYED;
        SetDelayStart(0);

        if (m_caster->HasUnitState(UNIT_STATE_CASTING) && !m_caster->IsNonMeleeSpellCast(false, false, true))
            m_caster->ClearUnitState(UNIT_STATE_CASTING);
    }
    else
    {
        // Immediate spell, no big deal
        handle_immediate();
    }

    // Take mods after triggered spells are handled (happened in HandleLaunchPhase)
    // otherwise triggered spells wouldn't profit from spellmods that already their
    // triggering spell used (needed for example for 14177 to affect 48664)
    // mods are taken only on succesfull cast and independantly from targets of the spell
	DropUsedModAurasCharges(false);

    ResetAttackTimerAfterCastIfNeeded();
    CallScriptAfterCastHandlers();

    if (const std::vector<int32> *spell_triggered = sSpellMgr->GetSpellLinked(m_spellInfo->Id))
    {
        for (std::vector<int32>::const_iterator i = spell_triggered->begin(); i != spell_triggered->end(); ++i)
            if (*i < 0)
                m_caster->RemoveAurasDueToSpell(-(*i));
            else
                m_caster->CastSpell(m_targets.GetUnitTarget() ? m_targets.GetUnitTarget() : m_caster, *i, true);
    }

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        m_caster->ToPlayer()->SetSpellModTakingSpell(this, false);

        //Clear spell cooldowns after every spell is cast if .cheat cooldown is enabled.
        if (m_caster->ToPlayer()->GetCommandStatus(CHEAT_COOLDOWN))
        {
            m_caster->GetSpellHistory()->ResetCooldown(m_spellInfo->Id, true);
            if (Player* player = m_caster->ToPlayer())
            {
                WorldPacket data(SMSG_CLEAR_COOLDOWN, 4 + 8);
                data << uint32(m_spellInfo->Id);
                data << uint64(player->GetGUID());
                player->SendDirectMessage(std::move(data));
            }
        }
    }

    // Interrupt Spell casting
    // handle this here, in other places SpellHitTarget can be set to NULL, if there is an error in this function
    if (m_spellInfo->HasAttribute(SPELL_ATTR7_INTERRUPT_ONLY_NONPLAYER))
        if (Unit* target = m_targets.GetUnitTarget())
            if (target->GetTypeId() == TYPEID_UNIT)
                m_caster->CastSpell(target, 32747, true);

    SetExecutedCurrently(false);
}

void Spell::handle_immediate()
{
    // start channeling if applicable
    if (m_spellInfo->IsChanneled())
    {
        int32 duration = m_spellInfo->GetDuration();
        if (duration > 0)
        {
            // First mod_duration then haste - see Missile Barrage
            // Apply duration mod
            if (Player* modOwner = m_caster->GetSpellModOwner())
                modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_DURATION, duration);

            // Apply haste mods
            if (m_spellInfo->HasAttribute(SPELL_ATTR5_HASTE_AFFECT_DURATION))
                m_caster->ModSpellCastTime(m_spellInfo, duration, this);

            m_spellState = SPELL_STATE_CASTING;
            m_caster->AddInterruptMask(m_spellInfo->ChannelInterruptFlags);
            m_channeledDuration = duration;
            SendChannelStart(duration);
        }
        else if (duration == -1)
        {
            m_spellState = SPELL_STATE_CASTING;
            m_caster->AddInterruptMask(m_spellInfo->ChannelInterruptFlags);
            SendChannelStart(duration);
        }
    }

    PrepareTargetProcessing();

    // process immediate effects (items, ground, etc.) also initialize some variables
    _handle_immediate_phase();

    for (std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
        DoAllEffectOnTarget(&(*ihit));

    for (std::list<GOTargetInfo>::iterator ihit= m_UniqueGOTargetInfo.begin(); ihit != m_UniqueGOTargetInfo.end(); ++ihit)
        DoAllEffectOnTarget(&(*ihit));

    FinishTargetProcessing();

    // spell is finished, perform some last features of the spell here
    _handle_finish_phase();

    // Remove used for cast item if need (it can be already NULL after TakeReagents call
    TakeCastItem();

    if (m_spellState != SPELL_STATE_CASTING)
        finish(true);                                       // successfully finish spell cast (not last in case autorepeat or channel spell)
}

uint64 Spell::handle_delayed(uint64 t_offset)
{
    if (!UpdatePointers())
    {
        // finish the spell if UpdatePointers() returned false, something wrong happened there
        finish(false);
        return 0;
    }

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
        m_caster->ToPlayer()->SetSpellModTakingSpell(this, true);

    uint64 next_time = 0;

    PrepareTargetProcessing();

    if (!m_immediateHandled)
    {
        _handle_immediate_phase();
        m_immediateHandled = true;
    }

    bool single_missile = (m_targets.HasDst());

    // now recheck units targeting correctness (need before any effects apply to prevent adding immunity at first effect not allow apply second spell effect and similar cases)
    for (std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if (ihit->processed == false)
        {
            if (single_missile || ihit->timeDelay <= t_offset)
            {
                ihit->timeDelay = t_offset;
                DoAllEffectOnTarget(&(*ihit));
            }
            else if (next_time == 0 || ihit->timeDelay < next_time)
                next_time = ihit->timeDelay;
        }
    }

    // now recheck gameobject targeting correctness
    for (std::list<GOTargetInfo>::iterator ighit= m_UniqueGOTargetInfo.begin(); ighit != m_UniqueGOTargetInfo.end(); ++ighit)
    {
        if (ighit->processed == false)
        {
            if (single_missile || ighit->timeDelay <= t_offset)
                DoAllEffectOnTarget(&(*ighit));
            else if (next_time == 0 || ighit->timeDelay < next_time)
                next_time = ighit->timeDelay;
        }
    }

    FinishTargetProcessing();

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
        m_caster->ToPlayer()->SetSpellModTakingSpell(this, false);

    // All targets passed - need finish phase
    if (next_time == 0)
    {
        // spell is finished, perform some last features of the spell here
        _handle_finish_phase();

        finish(true);                                       // successfully finish spell cast

        // return zero, spell is finished now
        return 0;
    }
    else
    {
        // spell is unfinished, return next execution time
        return next_time;
    }
}

void Spell::_handle_immediate_phase()
{
    m_spellAura = NULL;
    // initialize Diminishing Returns Data
    m_diminishLevel = DIMINISHING_LEVEL_1;
    m_diminishGroup = DIMINISHING_NONE;

    // handle some immediate features of the spell here
    HandleThreatSpells();

    PrepareScriptHitHandlers();

    // handle effects with SPELL_EFFECT_HANDLE_HIT mode
    for (uint32 j = 0; j < MAX_SPELL_EFFECTS; ++j)
    {
        // don't do anything for empty effect
        if (!m_spellInfo->Effects[j].IsEffect())
            continue;

        // call effect handlers to handle destination hit
        HandleEffects(NULL, NULL, NULL, j, SPELL_EFFECT_HANDLE_HIT);
    }

    // process items
    for (std::list<ItemTargetInfo>::iterator ihit= m_UniqueItemInfo.begin(); ihit != m_UniqueItemInfo.end(); ++ihit)
        DoAllEffectOnTarget(&(*ihit));

    if (!m_originalCaster)
        return;
    
    // @todo finish new fucking proc system
    if (m_UniqueTargetInfo.empty() && m_targets.HasDst())
    {
       uint32 procAttacker = m_procAttacker;
       if (!procAttacker)
           procAttacker |= PROC_FLAG_DONE_SPELL_MAGIC_DMG_CLASS_POS;

       // Proc the spells that have DEST target
       m_originalCaster->ProcDamageAndSpell(NULL, procAttacker, 0, m_procEx | PROC_EX_NORMAL_HIT, 0, 0, BASE_ATTACK, m_spellInfo, m_triggeredByAuraSpell);
    }
}

void Spell::_handle_finish_phase()
{
    if (m_caster->m_movedPlayer)
    {
        // Take for real after all targets are processed
        if (m_needComboPoints)
            m_caster->m_movedPlayer->ClearComboPoints();

        // Real add combo points from effects
        if (m_comboPointGain)
            m_caster->m_movedPlayer->GainSpellComboPoints(m_comboPointGain);
    }

    if (m_caster->m_extraAttacks && GetSpellInfo()->HasEffect(SPELL_EFFECT_ADD_EXTRA_ATTACKS))
        m_caster->HandleProcExtraAttackFor(m_caster->GetVictim());

    /// @todo trigger proc phase finish here
}

void Spell::SendSpellCooldown()
{
    m_caster->GetSpellHistory()->HandleCooldowns(m_spellInfo, m_CastItem, this);
}

void Spell::update(uint32 difftime)
{
    // update pointers based at it's GUIDs
    if (!UpdatePointers())
    {
        // cancel the spell if UpdatePointers() returned false, something wrong happened there
        cancel();
        return;
    }

    if (m_targets.GetUnitTargetGUID() && !m_targets.GetUnitTarget())
    {
        TC_LOG_DEBUG("spells", "Spell %u is cancelled due to removal of target.", m_spellInfo->Id);
        cancel();
        return;
    }

    *this <<= Fire::ProcessEvents.Bind(DiffParam(difftime));

    // check if the player caster has moved before the spell finished
    if ((m_caster->GetTypeId() == TYPEID_PLAYER && m_timer != 0) &&
        m_caster->isMoving() && (m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_MOVEMENT &&
        (m_spellInfo->Effects[0].Effect != SPELL_EFFECT_STUCK || !m_caster->HasUnitMovementFlag(MOVEMENTFLAG_FALLING_FAR))))
    {
        // don't cancel for melee, autorepeat, triggered and instant spells
        if (!IsNextMeleeSwingSpell() && !IsAutoRepeat() && !IsTriggered() && !(IsChannelActive() && m_spellInfo->HasAttribute(SPELL_ATTR5_CAN_CHANNEL_WHEN_MOVING)))
        {
            // if charmed by creature, trust the AI not to cheat and allow the cast to proceed
            // @todo this is a hack, "creature" movesplines don't differentiate turning/moving right now
            // however, checking what type of movement the spline is for every single spline would be really expensive
            if (!m_caster->GetCharmerGUID().IsCreature())
                cancel();
        }
    }

    switch (m_spellState)
    {
        case SPELL_STATE_PREPARING:
        {
            if (m_timer > 0)
            {
                if (difftime >= (uint32)m_timer)
                    m_timer = 0;
                else
                    m_timer -= difftime;
            }

            if (m_timer == 0 && !IsNextMeleeSwingSpell() && !IsAutoRepeat())
                // don't CheckCast for instant spells - done in spell::prepare, skip duplicate checks, needed for range checks for example
                cast(!m_casttime);
            break;
        }
        case SPELL_STATE_CASTING:
        {
            if (m_timer)
            {
                // check if there are alive targets left
                if (!UpdateChanneledTargetList())
                {
                    TC_LOG_DEBUG("spells", "Channeled spell %d is removed due to lack of targets", m_spellInfo->Id);
                    SendChannelUpdate(0);
                    finish();
                }

                if (m_timer > 0)
                {
                    if (difftime >= (uint32)m_timer)
                        m_timer = 0;
                    else
                        m_timer -= difftime;
                }
            }

            if (m_timer == 0)
            {
                SendChannelUpdate(0);
                finish();
            }
            break;
        }
        default:
            break;
    }
}

void Spell::finish(bool ok)
{
    if (!m_caster)
        return;

    if (m_spellState == SPELL_STATE_FINISHED)
        return;
    m_spellState = SPELL_STATE_FINISHED;

    if (m_spellInfo->IsChanneled())
        m_caster->UpdateInterruptMask();

    if (m_caster->HasUnitState(UNIT_STATE_CASTING) && !m_caster->IsNonMeleeSpellCast(false, false, true))
        m_caster->ClearUnitState(UNIT_STATE_CASTING);

    // Unsummon summon as possessed creatures on spell cancel
    if (m_spellInfo->IsChanneled() && m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        if (Unit* charm = m_caster->GetCharm())
            if (charm->GetTypeId() == TYPEID_UNIT
                && charm->ToCreature()->HasUnitTypeMask(UNIT_MASK_PUPPET)
                && charm->GetUInt32Value(UNIT_CREATED_BY_SPELL) == m_spellInfo->Id)
                ((Puppet*)charm)->UnSummon();
    }

    if (Creature* creatureCaster = m_caster->ToCreature())
        creatureCaster->ReleaseFocus(this);

    if (!ok)
        return;

    if (m_caster->GetTypeId() == TYPEID_UNIT && m_caster->IsSummon())
    {
        // Unsummon statue
        uint32 spell = m_caster->GetUInt32Value(UNIT_CREATED_BY_SPELL);
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spell);
        if (spellInfo && spellInfo->SpellIconID == 2056)
        {
            TC_LOG_DEBUG("spells", "Statue %d is unsummoned in spell %d finish", m_caster->GetGUID().GetCounter(), m_spellInfo->Id);
            m_caster->setDeathState(JUST_DIED);
            return;
        }
    }

    // potions disabled by client, send event "not in combat" if need
    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        if (!m_triggeredByAuraSpell)
            m_caster->ToPlayer()->UpdatePotionCooldown(this);

        // triggered spell pointer can be not set in some cases
        // this is needed for proper apply of triggered spell mods
        m_caster->ToPlayer()->SetSpellModTakingSpell(this, true);

        // Take mods after trigger spell (needed for 14177 to affect 48664)
        // mods are taken only on succesfull cast and independantly from targets of the spell
        DropUsedModAurasCharges(true);
        m_caster->ToPlayer()->SetSpellModTakingSpell(this, false);
    }

    // Stop Attack for some spells
    if (m_spellInfo->HasAttribute(SPELL_ATTR0_STOP_ATTACK_TARGET))
        m_caster->AttackStop();

    *this <<= Fire::CastSuccess;
}

void Spell::WriteCastResultInfo(WorldPacket& data, Player* caster, SpellInfo const* spellInfo, uint8 castCount, SpellCastResult result, SpellCustomErrors customError)
{
    data << uint8(castCount);                               // single cast or multi 2.3 (0/1)

    data << uint32(spellInfo->Id);
    data << uint8(result);                                  // problem
    switch (result)
    {
        case SPELL_FAILED_REQUIRES_SPELL_FOCUS:
            data << uint32(spellInfo->RequiresSpellFocus);  // SpellFocusObject.dbc id
            break;
        case SPELL_FAILED_REQUIRES_AREA:                    // AreaTable.dbc id
            // hardcode areas limitation case
            switch (spellInfo->Id)
            {
                case 41617:                                 // Cenarion Mana Salve
                case 41619:                                 // Cenarion Healing Salve
                    data << uint32(3905);
                    break;
                case 41618:                                 // Bottled Nethergon Energy
                case 41620:                                 // Bottled Nethergon Vapor
                    data << uint32(3842);
                    break;
                case 45373:                                 // Bloodberry Elixir
                    data << uint32(4075);
                    break;
                default:                                    // default case (don't must be)
                    data << uint32(0);
                    break;
            }
            break;
        case SPELL_FAILED_TOTEMS:
            if (spellInfo->Totem[0])
                data << uint32(spellInfo->Totem[0]);
            if (spellInfo->Totem[1])
                data << uint32(spellInfo->Totem[1]);
            break;
        case SPELL_FAILED_TOTEM_CATEGORY:
            if (spellInfo->TotemCategory[0])
                data << uint32(spellInfo->TotemCategory[0]);
            if (spellInfo->TotemCategory[1])
                data << uint32(spellInfo->TotemCategory[1]);
            break;
        case SPELL_FAILED_EQUIPPED_ITEM_CLASS:
        case SPELL_FAILED_EQUIPPED_ITEM_CLASS_MAINHAND:
        case SPELL_FAILED_EQUIPPED_ITEM_CLASS_OFFHAND:
            data << uint32(spellInfo->EquippedItemClass);
            data << uint32(spellInfo->EquippedItemSubClassMask);
            break;
        case SPELL_FAILED_TOO_MANY_OF_ITEM:
        {
             uint32 item = 0;
             for (int8 eff = 0; eff < MAX_SPELL_EFFECTS; eff++)
                 if (spellInfo->Effects[eff].ItemType)
                     item = spellInfo->Effects[eff].ItemType;
             ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item);
             if (proto && proto->ItemLimitCategory)
                 data << uint32(proto->ItemLimitCategory);
             break;
        }
        case SPELL_FAILED_CUSTOM_ERROR:
            data << uint32(customError);
            break;
        case SPELL_FAILED_REAGENTS:
        {
            uint32 missingItem = 0;
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; i++)
            {
                if (spellInfo->Reagent[i] <= 0)
                    continue;

                uint32 itemid    = spellInfo->Reagent[i];
                uint32 itemcount = spellInfo->ReagentCount[i];

                if (!caster->HasItemCount(itemid, itemcount))
                {
                    missingItem = itemid;
                    break;
                }
            }

            data << uint32(missingItem);  // first missing item
            break;
        }
        case SPELL_FAILED_PREVENTED_BY_MECHANIC:
            data << uint32(spellInfo->Mechanic);
            break;
        case SPELL_FAILED_NEED_EXOTIC_AMMO:
            data << uint32(spellInfo->EquippedItemSubClassMask);
            break;
        case SPELL_FAILED_NEED_MORE_ITEMS:
            data << uint32(0); // Item entry
            data << uint32(0); // Count
            break;
        case SPELL_FAILED_MIN_SKILL:
            data << uint32(0); // SkillLine.dbc Id
            data << uint32(0); // Amount
            break;
        case SPELL_FAILED_FISHING_TOO_LOW:
            data << uint32(0); // Skill level
            break;
        default:
            break;
    }
}

void Spell::SendCastResult(Player* caster, SpellInfo const* spellInfo, uint8 castCount, SpellCastResult result, SpellCustomErrors customError /*= SPELL_CUSTOM_ERROR_NONE*/)
{
    if (result == SPELL_CAST_OK)
        return;

    WorldPacket data(SMSG_CAST_FAILED, 1 + 4 + 1);
    WriteCastResultInfo(data, caster, spellInfo, castCount, result, customError);

    caster->SendDirectMessage(std::move(data));
}

void Spell::SendCastResult(SpellCastResult result)
{
    if (result == SPELL_CAST_OK)
        return;

    m_castResult = result;

    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    if (m_caster->ToPlayer()->GetSession()->PlayerLoading())  // don't send cast results at loading time
        return;
    SendCastResult(m_caster->ToPlayer(), m_spellInfo, m_cast_count, result, m_customError);
}

void Spell::SendPetCastResult(SpellCastResult result)
{
    if (result == SPELL_CAST_OK)
        return;

    Unit* owner = m_caster->GetCharmerOrOwner();
    if (!owner)
        return;

    Player* player = owner->ToPlayer();
    if (!player)
        return;

    WorldPacket data(SMSG_PET_CAST_FAILED, 1 + 4 + 1);
    WriteCastResultInfo(data, player, m_spellInfo, m_cast_count, result, m_customError);

    player->SendDirectMessage(std::move(data));
}

void Spell::SendSpellStart()
{
    if (!IsNeedSendToClient())
        return;

    //TC_LOG_DEBUG("spells", "Sending SMSG_SPELL_START id=%u", m_spellInfo->Id);

    uint32 castFlags = CAST_FLAG_UNKNOWN_2;
    uint32 schoolImmunityMask = m_caster->GetSchoolImmunityMask();
    uint32 mechanicImmunityMask = m_caster->GetMechanicImmunityMask();
    if (schoolImmunityMask || mechanicImmunityMask)
        castFlags |= CAST_FLAG_IMMUNITY;

    if (((IsTriggered() && !m_spellInfo->IsAutoRepeatRangedSpell()) || m_triggeredByAuraSpell) && !m_cast_count)
        castFlags |= CAST_FLAG_PENDING;

    if (m_spellInfo->HasAttribute(SPELL_ATTR0_REQ_AMMO) || m_spellInfo->HasAttribute(SPELL_ATTR0_CU_NEEDS_AMMO_DATA))
        castFlags |= CAST_FLAG_AMMO;
    if ((m_caster->GetTypeId() == TYPEID_PLAYER ||
        (m_caster->GetTypeId() == TYPEID_UNIT && m_caster->IsPet()))
         && m_spellInfo->PowerType != POWER_HEALTH)
        castFlags |= CAST_FLAG_POWER_LEFT_SELF;

    if (m_spellInfo->RuneCostID && m_spellInfo->PowerType == POWER_RUNE)
        castFlags |= CAST_FLAG_NO_GCD; // not needed, but Blizzard sends it

    WorldPacket data(SMSG_SPELL_START, (8+8+4+4+2));
    if (m_CastItem)
        data << m_CastItem->GetPackGUID();
    else
        data << m_caster->GetPackGUID();

    data << m_caster->GetPackGUID();
    data << uint8(m_cast_count);                            // pending spell cast?
    data << uint32(visualSpellId);                        // spellId
    data << uint32(castFlags);                              // cast flags
    data << int32(m_timer);                                 // delay?

    m_targets.Write(data);

    if (castFlags & CAST_FLAG_POWER_LEFT_SELF)
        data << uint32(m_caster->GetPower((Powers)m_spellInfo->PowerType));

    if (castFlags & CAST_FLAG_AMMO)
        WriteAmmoToPacket(&data);

    if (castFlags & CAST_FLAG_IMMUNITY)
    {
        data << uint32(schoolImmunityMask);
        data << uint32(mechanicImmunityMask);
    }

    m_caster->SendMessageToSet(data, true);
}

void Spell::SendSpellGo()
{
    // not send invisible spell casting
    if (!IsNeedSendToClient())
        return;

    //TC_LOG_DEBUG("spells", "Sending SMSG_SPELL_GO id=%u", m_spellInfo->Id);

    uint32 castFlags = CAST_FLAG_UNKNOWN_9;

    // triggered spells with spell visual != 0
    if (((IsTriggered() && !m_spellInfo->IsAutoRepeatRangedSpell()) || m_triggeredByAuraSpell) && !m_cast_count)
        castFlags |= CAST_FLAG_PENDING;

    //! @ToDo: Find a more generic way for these spells
    if (m_spellInfo->HasAttribute(SPELL_ATTR0_REQ_AMMO) || m_spellInfo->HasAttribute(SPELL_ATTR0_CU_NEEDS_AMMO_DATA))
        castFlags |= CAST_FLAG_AMMO;                        // arrows/bullets visual

    if ((m_caster->GetTypeId() == TYPEID_PLAYER ||
        (m_caster->GetTypeId() == TYPEID_UNIT && m_caster->IsPet()))
        && m_spellInfo->PowerType != POWER_HEALTH)
        castFlags |= CAST_FLAG_POWER_LEFT_SELF; // should only be sent to self, but the current messaging doesn't make that possible

    if ((m_caster->GetTypeId() == TYPEID_PLAYER)
        && (m_caster->getClass() == CLASS_DEATH_KNIGHT)
        && m_spellInfo->RuneCostID
        && m_spellInfo->PowerType == POWER_RUNE
        && !(_triggeredCastFlags & TRIGGERED_IGNORE_POWER_AND_REAGENT_COST))
    {
        castFlags |= CAST_FLAG_NO_GCD;                       // not needed, but Blizzard sends it
        castFlags |= CAST_FLAG_RUNE_LIST;                    // rune cooldowns list
    }

    if (m_spellInfo->HasEffect(SPELL_EFFECT_ACTIVATE_RUNE))
        castFlags |= CAST_FLAG_RUNE_LIST;                    // rune cooldowns list

    if (m_targets.HasTraj())
        castFlags |= CAST_FLAG_ADJUST_MISSILE;

    if (!m_spellInfo->StartRecoveryTime)
        castFlags |= CAST_FLAG_NO_GCD;

    WorldPacket data(SMSG_SPELL_GO, 50);                    // guess size

    if (m_CastItem)
        data << m_CastItem->GetPackGUID();
    else
        data << m_caster->GetPackGUID();

    data << m_caster->GetPackGUID();
    data << uint8(m_cast_count);                            // pending spell cast?
    data << uint32(visualSpellId);                          // spellId
    data << uint32(castFlags);                              // cast flags
    data << uint32(game::time::GetGameTimeMS());              // timestamp

    WriteSpellGoTargets(&data);

    m_targets.Write(data);

    if (castFlags & CAST_FLAG_POWER_LEFT_SELF)
        data << uint32(m_caster->GetPower((Powers)m_spellInfo->PowerType));

    if (castFlags & CAST_FLAG_RUNE_LIST)                   // rune cooldowns list
    {
        /// @todo There is a crash caused by a spell with CAST_FLAG_RUNE_LIST cast by a creature
        //The creature is the mover of a player, so HandleCastSpellOpcode uses it as the caster
        if (Player* player = m_caster->ToPlayer())
        {
            uint8 runeMaskInitial = m_runesState;
            uint8 runeMaskAfterCast = player->GetRunesState();
            data << uint8(runeMaskInitial);                     // runes state before
            data << uint8(runeMaskAfterCast);                   // runes state after
            for (uint8 i = 0; i < MAX_RUNES; ++i)
            {
                uint8 mask = (1 << i);
                if (mask & runeMaskInitial && !(mask & runeMaskAfterCast))  // usable before andon cooldown now...
                {
                    // float casts ensure the division is performed on floats as we need float result
                    float baseCd = float(player->GetRuneBaseCooldown(i));
                    data << uint8((baseCd - float(player->GetRuneCooldown(i))) / baseCd * 255); // rune cooldown passed
                }
            }
        }
    }
    if (castFlags & CAST_FLAG_ADJUST_MISSILE)
    {
        data << m_targets.GetElevation();
        data << uint32(m_delayMoment);
    }

    if (castFlags & CAST_FLAG_AMMO)
        WriteAmmoToPacket(&data);

    if (castFlags & CAST_FLAG_VISUAL_CHAIN)
    {
        data << uint32(0);
        data << uint32(0);
    }

    if (m_targets.GetTargetMask() & TARGET_FLAG_DEST_LOCATION)
    {
        data << uint8(0);
    }

    m_caster->SendMessageToSet(data, true);
}

void Spell::WriteAmmoToPacket(WorldPacket* data)
{
    uint32 ammoInventoryType = 0;
    uint32 ammoDisplayID = 0;

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        Item* pItem = m_caster->ToPlayer()->GetWeaponForAttack(RANGED_ATTACK);
        if (pItem)
        {
            ammoInventoryType = pItem->GetTemplate()->InventoryType;
            if (ammoInventoryType == INVTYPE_THROWN)
                ammoDisplayID = pItem->GetTemplate()->DisplayInfoID;
            else
            {
                uint32 ammoID = m_caster->ToPlayer()->GetUInt32Value(PLAYER_AMMO_ID);
                if (ammoID)
                {
                    ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(ammoID);
                    if (pProto)
                    {
                        ammoDisplayID = pProto->DisplayInfoID;
                        ammoInventoryType = pProto->InventoryType;
                    }
                }
                else if (m_caster->HasAura(46699))      // Requires No Ammo
                {
                    ammoDisplayID = 5996;                   // normal arrow
                    ammoInventoryType = INVTYPE_AMMO;
                }
            }
        }
    }
    else
    {
        for (uint8 i = 0; i < 3; ++i)
        {
            if (uint32 item_id = m_caster->GetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_ID + i))
            {
                if (ItemEntry const* itemEntry = sItemStore.LookupEntry(item_id))
                {
                    if (itemEntry->Class == ITEM_CLASS_WEAPON)
                    {
                        switch (itemEntry->SubClass)
                        {
                            case ITEM_SUBCLASS_WEAPON_THROWN:
                                ammoDisplayID = itemEntry->DisplayId;
                                ammoInventoryType = itemEntry->InventoryType;
                                break;
                            case ITEM_SUBCLASS_WEAPON_BOW:
                            case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                                ammoDisplayID = 5996;       // is this need fixing?
                                ammoInventoryType = INVTYPE_AMMO;
                                break;
                            case ITEM_SUBCLASS_WEAPON_GUN:
                                ammoDisplayID = 5998;       // is this need fixing?
                                ammoInventoryType = INVTYPE_AMMO;
                                break;
                        }

                        if (ammoDisplayID)
                            break;
                    }
                }
            }
        }
    }

    *data << uint32(ammoDisplayID);
    *data << uint32(ammoInventoryType);
}

/// Writes miss and hit targets for a SMSG_SPELL_GO packet
void Spell::WriteSpellGoTargets(WorldPacket* data)
{
    // This function also fill data for channeled spells:
    // m_needAliveTargetMask req for stop channelig if one target die
    for (std::list<TargetInfo>::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if ((*ihit).effectMask == 0)                  // No effect apply - all immuned add state
            // possibly SPELL_MISS_IMMUNE2 for this??
            ihit->missCondition = SPELL_MISS_IMMUNE2;
    }

    // Hit and miss target counts are both uint8, that limits us to 255 targets for each
    // sending more than 255 targets crashes the client (since count sent would be wrong)
    // Spells like 40647 (with a huge radius) can easily reach this limit (spell might need
    // target conditions but we still need to limit the number of targets sent and keeping
    // correct count for both hit and miss).

    uint32 hit = 0;
    size_t hitPos = data->wpos();
    *data << (uint8)0; // placeholder
    for (std::list<TargetInfo>::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end() && hit < 255; ++ihit)
    {
        if ((*ihit).missCondition == SPELL_MISS_NONE)       // Add only hits
        {
            *data << uint64(ihit->targetGUID);
            m_channelTargetEffectMask |=ihit->effectMask;
            ++hit;
        }
    }

    for (std::list<GOTargetInfo>::const_iterator ighit = m_UniqueGOTargetInfo.begin(); ighit != m_UniqueGOTargetInfo.end() && hit < 255; ++ighit)
    {
        *data << uint64(ighit->targetGUID);                 // Always hits
        ++hit;
    }

    uint32 miss = 0;
    size_t missPos = data->wpos();
    *data << (uint8)0; // placeholder
    for (std::list<TargetInfo>::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end() && miss < 255; ++ihit)
    {
        if (ihit->missCondition != SPELL_MISS_NONE)        // Add only miss
        {
            *data << uint64(ihit->targetGUID);
            *data << uint8(ihit->missCondition);
            if (ihit->missCondition == SPELL_MISS_REFLECT)
                *data << uint8(ihit->reflectResult);
            ++miss;
        }
    }
    // Reset m_needAliveTargetMask for non channeled spell
    if (!m_spellInfo->IsChanneled())
        m_channelTargetEffectMask = 0;

    data->put<uint8>(hitPos, (uint8)hit);
    data->put<uint8>(missPos, (uint8)miss);
}

void Spell::SendLogExecute()
{
    WorldPacket data(SMSG_SPELLLOGEXECUTE, (8+4+4+4+4+8));

    data << m_caster->GetPackGUID();

    data << uint32(m_spellInfo->Id);

    uint8 effCount = 0;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        if (m_effectExecuteData[i])
            ++effCount;
    }

    if (!effCount)
        return;

    data << uint32(effCount);
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        if (!m_effectExecuteData[i])
            continue;

        data << uint32(m_spellInfo->Effects[i].Effect);             // spell effect

        data.append(*m_effectExecuteData[i]);

        delete m_effectExecuteData[i];
        m_effectExecuteData[i] = NULL;
    }
    m_caster->SendMessageToSet(data, true);
}

void Spell::ExecuteLogEffectTakeTargetPower(uint8 effIndex, Unit* target, uint32 powerType, uint32 powerTaken, float gainMultiplier)
{
    InitEffectExecuteData(effIndex);
    *m_effectExecuteData[effIndex] << target->GetPackGUID();
    *m_effectExecuteData[effIndex] << uint32(powerTaken);
    *m_effectExecuteData[effIndex] << uint32(powerType);
    *m_effectExecuteData[effIndex] << float(gainMultiplier);
}

void Spell::ExecuteLogEffectExtraAttacks(uint8 effIndex, Unit* victim, uint32 attCount)
{
    InitEffectExecuteData(effIndex);
    *m_effectExecuteData[effIndex] << victim->GetPackGUID();
    *m_effectExecuteData[effIndex] << uint32(attCount);
}

void Spell::ExecuteLogEffectInterruptCast(uint8 effIndex, Unit* victim, uint32 spellId)
{
    InitEffectExecuteData(effIndex);
    *m_effectExecuteData[effIndex] << victim->GetPackGUID();
    *m_effectExecuteData[effIndex] << uint32(spellId);
}

void Spell::ExecuteLogEffectDurabilityDamage(uint8 effIndex, Unit* victim, int32 itemId, int32 slot)
{
    InitEffectExecuteData(effIndex);
    *m_effectExecuteData[effIndex] << victim->GetPackGUID();
    *m_effectExecuteData[effIndex] << int32(itemId);
    *m_effectExecuteData[effIndex] << int32(slot);
}

void Spell::ExecuteLogEffectOpenLock(uint8 effIndex, Object* obj)
{
    InitEffectExecuteData(effIndex);
    *m_effectExecuteData[effIndex] << obj->GetPackGUID();
}

void Spell::ExecuteLogEffectCreateItem(uint8 effIndex, uint32 entry)
{
    InitEffectExecuteData(effIndex);
    *m_effectExecuteData[effIndex] << uint32(entry);
}

void Spell::ExecuteLogEffectDestroyItem(uint8 effIndex, uint32 entry)
{
    InitEffectExecuteData(effIndex);
    *m_effectExecuteData[effIndex] << uint32(entry);
}

void Spell::ExecuteLogEffectSummonObject(uint8 effIndex, WorldObject* obj)
{
    InitEffectExecuteData(effIndex);
    *m_effectExecuteData[effIndex] << obj->GetPackGUID();
}

void Spell::ExecuteLogEffectUnsummonObject(uint8 effIndex, WorldObject* obj)
{
    InitEffectExecuteData(effIndex);
    *m_effectExecuteData[effIndex] << obj->GetPackGUID();
}

void Spell::ExecuteLogEffectResurrect(uint8 effIndex, Unit* target)
{
    InitEffectExecuteData(effIndex);
    *m_effectExecuteData[effIndex] << target->GetPackGUID();
}

void Spell::SendInterrupted(uint8 result)
{
    WorldPacket data(SMSG_SPELL_FAILURE, (8+4+1));
    data << m_caster->GetPackGUID();
    data << uint8(m_cast_count);
    data << uint32(m_spellInfo->Id);
    data << uint8(result);
    m_caster->SendMessageToSet(data, true);

    data.Initialize(SMSG_SPELL_FAILED_OTHER, (8+4));
    data << m_caster->GetPackGUID();
    data << uint8(m_cast_count);
    data << uint32(m_spellInfo->Id);
    data << uint8(result);
    m_caster->SendMessageToSet(data, true);
}

void Spell::SendChannelUpdate(uint32 time)
{
    if (time == 0)
    {
        m_caster->SetChannelObjectGuid(ObjectGuid::Empty);
        m_caster->SetUInt32Value(UNIT_CHANNEL_SPELL, 0);
    }

    WorldPacket data(MSG_CHANNEL_UPDATE, 8+4);
    data << m_caster->GetPackGUID();
    data << uint32(time);

    m_caster->SendMessageToSet(data, true);
}

void Spell::SendChannelStart(uint32 duration)
{
    ObjectGuid channelTarget = m_targets.GetObjectTargetGUID();
    if (!channelTarget && !m_spellInfo->NeedsExplicitUnitTarget())
        if (m_UniqueTargetInfo.size() + m_UniqueGOTargetInfo.size() == 1)   // this is for TARGET_SELECT_CATEGORY_NEARBY
            channelTarget = !m_UniqueTargetInfo.empty() ? m_UniqueTargetInfo.front().targetGUID : m_UniqueGOTargetInfo.front().targetGUID;

    WorldPacket data(MSG_CHANNEL_START, (8+4+4));
    data << m_caster->GetPackGUID();
    data << uint32(m_spellInfo->Id);
    data << uint32(duration);

    m_caster->SendMessageToSet(data, true);

    m_timer = duration;
    if (channelTarget)
    {
        m_caster->lastChannelTarget = channelTarget;
        m_caster->SetChannelObjectGuid(channelTarget);
    }

    m_caster->SetUInt32Value(UNIT_CHANNEL_SPELL, m_spellInfo->Id);
}

void Spell::SendResurrectRequest(Player* target)
{
    // get resurrector name for creature resurrections, otherwise packet will be not accepted
    // for player resurrections the name is looked up by guid
    const std::string_view sentName = m_caster->GetTypeId() == TYPEID_PLAYER ?
        "" : m_caster->GetNameForLocaleIdx(target->GetSession()->GetSessionDbLocaleIndex());

    WorldPacket data(SMSG_RESURRECT_REQUEST, 8 + 4 + sentName.size() + 1 + 1 + 1);
    data << uint64(m_caster->GetGUID());
    data << uint32(sentName.size() + 1);
    data << sentName.data();
    data << uint8(m_caster->IsSpiritHealer()); // "you'll be afflicted with resurrection sickness"
    // override delay sent with SMSG_CORPSE_RECLAIM_DELAY, set instant resurrection for spells with this attribute
    data << uint8(!(m_spellInfo->AttributesEx3 & SPELL_ATTR3_IGNORE_RESURRECTION_TIMER));
    target->SendDirectMessage(std::move(data));
}

void Spell::TakeCastItem()
{
    if (!m_CastItem)
        return;

    Player* player = m_caster->ToPlayer();
    if (!player)
        return;

    // not remove cast item at triggered spell (equipping, weapon damage, etc)
    if (_triggeredCastFlags & TRIGGERED_IGNORE_CAST_ITEM)
        return;

    ItemTemplate const* proto = m_CastItem->GetTemplate();

    if (!proto)
    {
        // This code is to avoid a crash
        // I'm not sure, if this is really an error, but I guess every item needs a prototype
        TC_LOG_ERROR("spells", "Cast item has no item prototype: %s", m_CastItem->GetGUID().ToString().c_str());
        return;
    }

    bool expendable = false;
    bool withoutCharges = false;

    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (proto->Spells[i].SpellId)
        {
            // item has limited charges
            if (proto->Spells[i].SpellCharges)
            {
                if (proto->Spells[i].SpellCharges < 0)
                    expendable = true;

                int32 charges = m_CastItem->GetSpellCharges(i);

                // item has charges left
                if (charges)
                {
                    (charges > 0) ? --charges : ++charges;  // abs(charges) less at 1 after use
                    if (proto->Stackable == 1)
                        m_CastItem->SetSpellCharges(i, charges);
                    m_CastItem->SetState(ITEM_CHANGED, player);
                }

                // all charges used
                withoutCharges = (charges == 0);
            }
        }
    }

    if (expendable && withoutCharges)
    {
        uint32 count = 1;
        // RG-Custom ItemLogging
        RG_LOG<ItemLogModule>(m_CastItem, RG::Logging::ItemLogType::DESTROYED_BY_USE, count);
        m_caster->ToPlayer()->DestroyItemCount(m_CastItem, count, true);

        // prevent crash at access to deleted m_targets.GetItemTarget
        if (m_CastItem == m_targets.GetItemTarget())
            m_targets.SetItemTarget(NULL);

        m_CastItem = NULL;
        m_castItemGUID.Clear();
        m_castItemEntry = 0;
    }
}

void Spell::TakePower()
{
    if (m_CastItem || m_triggeredByAuraSpell)
        return;

    //Don't take power if the spell is cast while .cheat power is enabled.
    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        if (m_caster->ToPlayer()->GetCommandStatus(CHEAT_POWER))
            return;
    }

    Powers powerType = Powers(m_spellInfo->PowerType);
    bool hit = true;
    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        if (powerType == POWER_RAGE || powerType == POWER_ENERGY || powerType == POWER_RUNE)
            if (ObjectGuid targetGUID = m_targets.GetUnitTargetGUID())
                for (std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
                    if (ihit->targetGUID == targetGUID)
                    {
                        if (ihit->missCondition != SPELL_MISS_NONE)
                        {
                            hit = false;
                            //lower spell cost on fail (by talent aura)
                            if (Player* modOwner = m_caster->ToPlayer()->GetSpellModOwner())
                                modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_SPELL_COST_REFUND_ON_FAIL, m_powerCost);
                        }
                        break;
                    }
    }

    if (powerType == POWER_RUNE)
    {
        TakeRunePower(hit);
        return;
    }

    if (!m_powerCost)
        return;

    // health as power used
    if (powerType == POWER_HEALTH)
    {
        m_caster->ModifyHealth(-(int32)m_powerCost);
        return;
    }

    if (powerType >= MAX_POWERS)
    {
        TC_LOG_ERROR("spells", "Spell::TakePower: Unknown power type '%d'", powerType);
        return;
    }

    if (hit)
        m_caster->ModifyPower(powerType, -m_powerCost);
    else
        m_caster->ModifyPower(powerType, -irand(0, m_powerCost/4));
        
    // Check if we have enough power to cast next queued spell (SPELL_ATTR_ON_NEXT_SWING_1)
    if (Spell* queuedSpell = m_caster->GetCurrentSpell(CURRENT_MELEE_SPELL))
    {
        uint32 curPower = m_caster->GetPower(m_caster->getPowerType());
        int32 reqPower = queuedSpell->GetPowerCost();

        if (queuedSpell->m_spellInfo->HasAttribute(SPELL_ATTR0_ON_NEXT_SWING) && int32(curPower) < reqPower)
            m_caster->InterruptSpell(CURRENT_MELEE_SPELL);
    }

    // Set the five second timer
    if (powerType == POWER_MANA && m_powerCost > 0)
        m_caster->SetLastManaUse(game::time::GetGameTimeMS());
}

void Spell::TakeAmmo()
{
    if (m_attackType == RANGED_ATTACK && m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        Item* pItem = m_caster->ToPlayer()->GetWeaponForAttack(RANGED_ATTACK);

        // wands don't have ammo
        if (!pItem  || pItem->IsBroken() || pItem->GetTemplate()->SubClass == ITEM_SUBCLASS_WEAPON_WAND)
            return;

        if (pItem->GetTemplate()->InventoryType == INVTYPE_THROWN)
        {
            if (pItem->GetMaxStackCount() == 1)
            {
                // decrease durability for non-stackable throw weapon
                m_caster->ToPlayer()->DurabilityPointLossForEquipSlot(EQUIPMENT_SLOT_RANGED);
            }
            else
            {
                // decrease items amount for stackable throw weapon
                uint32 count = 1;
                // RG-Custom ItemLogging
                RG_LOG<ItemLogModule>(pItem, RG::Logging::ItemLogType::DESTROYED_BY_USE, count);
                m_caster->ToPlayer()->DestroyItemCount(pItem, count, true);
            }
        }
        else if (uint32 ammo = m_caster->ToPlayer()->GetUInt32Value(PLAYER_AMMO_ID))
            m_caster->ToPlayer()->DestroyItemCount(ammo, 1, true);
    }
}

SpellCastResult Spell::CheckRuneCost(uint32 runeCostID)
{
    if (m_spellInfo->PowerType != POWER_RUNE || !runeCostID)
        return SPELL_CAST_OK;

    Player* player = m_caster->ToPlayer();
    if (!player)
        return SPELL_CAST_OK;

    if (player->getClass() != CLASS_DEATH_KNIGHT)
        return SPELL_CAST_OK;

    SpellRuneCostEntry const* src = sSpellRuneCostStore.LookupEntry(runeCostID);
    if (!src)
        return SPELL_CAST_OK;

    if (src->NoRuneCost())
        return SPELL_CAST_OK;

    int32 runeCost[NUM_RUNE_TYPES];                         // blood, frost, unholy, death

    for (uint32 i = 0; i < RUNE_DEATH; ++i)
    {
        runeCost[i] = src->RuneCost[i];
        if (Player* modOwner = m_caster->GetSpellModOwner())
            modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_COST, runeCost[i], this);
    }

    runeCost[RUNE_DEATH] = MAX_RUNES;                       // calculated later

    for (uint32 i = 0; i < MAX_RUNES; ++i)
    {
        RuneType rune = player->GetCurrentRune(i);
        if ((player->GetRuneCooldown(i) == 0) && (runeCost[rune] > 0))
            runeCost[rune]--;
    }

    for (uint32 i = 0; i < RUNE_DEATH; ++i)
        if (runeCost[i] > 0)
            runeCost[RUNE_DEATH] += runeCost[i];

    if (runeCost[RUNE_DEATH] > MAX_RUNES)
        return SPELL_FAILED_NO_POWER;                       // not sure if result code is correct

    return SPELL_CAST_OK;
}

void Spell::TakeRunePower(bool didHit)
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER || m_caster->getClass() != CLASS_DEATH_KNIGHT)
        return;

    SpellRuneCostEntry const* runeCostData = sSpellRuneCostStore.LookupEntry(m_spellInfo->RuneCostID);
    if (!runeCostData || (runeCostData->NoRuneCost() && runeCostData->NoRunicPowerGain()))
        return;

    Player* player = m_caster->ToPlayer();
    m_runesState = player->GetRunesState();                 // store previous state

    int32 runeCost[NUM_RUNE_TYPES];                         // blood, frost, unholy, death

    for (uint32 i = 0; i < RUNE_DEATH; ++i)
    {
        runeCost[i] = runeCostData->RuneCost[i];
        if (Player* modOwner = m_caster->GetSpellModOwner())
            modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_COST, runeCost[i], this);
    }

    runeCost[RUNE_DEATH] = 0;                               // calculated later

    for (uint32 i = 0; i < MAX_RUNES; ++i)
    {
        RuneType rune = player->GetCurrentRune(i);
        if (!player->GetRuneCooldown(i) && runeCost[rune] > 0)
        {
            player->SetRuneCooldown(i, didHit ? player->GetRuneBaseCooldown(i) : uint32(RUNE_MISS_COOLDOWN), true);
            runeCost[rune]--;
        }
    }

    runeCost[RUNE_DEATH] = runeCost[RUNE_BLOOD] + runeCost[RUNE_UNHOLY] + runeCost[RUNE_FROST];
    if (runeCost[RUNE_DEATH] > 0)
    {
        for (uint32 i = 0; i < MAX_RUNES; ++i)
        {
            RuneType rune = player->GetCurrentRune(i);
            RuneType baseRune = player->GetBaseRune(i);
            if (!player->GetRuneCooldown(i) && rune == RUNE_DEATH && runeCost[baseRune] > 0)
            {
                player->SetRuneCooldown(i, didHit ? player->GetRuneBaseCooldown(i) : static_cast<uint32>(RUNE_MISS_COOLDOWN));

                runeCost[baseRune]--;
                runeCost[rune]--;

                // keep Death Rune type if missed
                if (didHit)
                    player->RestoreBaseRune(i);

                if (runeCost[RUNE_DEATH] == 0)
                    break;
            }
        }
    }

    // Grab any Death rune
    if (runeCost[RUNE_DEATH] > 0)
    {
        for (uint32 i = 0; i < MAX_RUNES; ++i)
        {
            RuneType rune = player->GetCurrentRune(i);
            if (!player->GetRuneCooldown(i) && rune == RUNE_DEATH)
            {
                player->SetRuneCooldown(i, didHit ? player->GetRuneBaseCooldown(i) : uint32(RUNE_MISS_COOLDOWN), true);

                runeCost[rune]--;

                // keep Death Rune type if missed
                if (didHit)
                    player->RestoreBaseRune(i);

                if (runeCost[RUNE_DEATH] == 0)
                    break;
            }
        }
    }

    // you can gain some runic power when use runes
    if (didHit)
        if (int32 rp = int32(runeCostData->runePowerGain * sWorld->getRate(RATE_POWER_RUNICPOWER_INCOME)))
            player->ModifyPower(POWER_RUNIC_POWER, int32(rp));
}

void Spell::TakeReagents()
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    ItemTemplate const* castItemTemplate = m_CastItem ? m_CastItem->GetTemplate() : NULL;

    // do not take reagents for these item casts
    if (castItemTemplate && castItemTemplate->Flags & ITEM_PROTO_FLAG_TRIGGERED_CAST)
        return;

    Player* p_caster = m_caster->ToPlayer();
    if (p_caster->CanNoReagentCast(m_spellInfo))
        return;

    for (uint32 x = 0; x < MAX_SPELL_REAGENTS; ++x)
    {
        if (m_spellInfo->Reagent[x] <= 0)
            continue;

        uint32 itemid = m_spellInfo->Reagent[x];
        uint32 itemcount = m_spellInfo->ReagentCount[x];

        // if CastItem is also spell reagent
        if (castItemTemplate && castItemTemplate->ItemId == itemid)
        {
            for (int s = 0; s < MAX_ITEM_PROTO_SPELLS; ++s)
            {
                // CastItem will be used up and does not count as reagent
                int32 charges = m_CastItem->GetSpellCharges(s);
                if (castItemTemplate->Spells[s].SpellCharges < 0 && abs(charges) < 2)
                {
                    ++itemcount;
                    break;
                }
            }

            m_CastItem = NULL;
            m_castItemGUID.Clear();
            m_castItemEntry = 0;
        }

        // if GetItemTarget is also spell reagent
        if (m_targets.GetItemTargetEntry() == itemid)
            m_targets.SetItemTarget(NULL);

        p_caster->DestroyItemCount(itemid, itemcount, true);
    }
}

void Spell::HandleThreatSpells()
{
    if (m_UniqueTargetInfo.empty())
        return;

    if (m_spellInfo->HasAttribute(SPELL_ATTR1_NO_THREAT) ||
        m_spellInfo->HasAttribute(SPELL_ATTR3_NO_INITIAL_AGGRO))
        return;

    float threat = 0.0f;
    if (SpellThreatEntry const* threatEntry = sSpellMgr->GetSpellThreatEntry(m_spellInfo->Id))
    {
        if (threatEntry->apPctMod != 0.0f)
            threat += threatEntry->apPctMod * m_caster->GetTotalAttackPowerValue(BASE_ATTACK);

        threat += threatEntry->flatMod;
    }
    else if (m_spellInfo->HasAttribute(SPELL_ATTR0_CU_NO_INITIAL_THREAT) == 0)
        threat += m_spellInfo->SpellLevel;

    // past this point only multiplicative effects occur
    if (threat == 0.0f)
        return;

    // since 2.0.1 threat from positive effects also is distributed among all targets, so the overall caused threat is at most the defined bonus
    threat /= m_UniqueTargetInfo.size();

    for (std::list<TargetInfo>::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        float threatToAdd = threat;
        if (ihit->missCondition != SPELL_MISS_NONE)
            threatToAdd = 0.0f;

        Unit* target = ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID);
        if (!target)
            continue;

        // positive spells distribute threat among all units that are in combat with target, like healing
        if (m_spellInfo->_IsPositiveSpell())
            target->GetThreatManager().ForwardThreatForAssistingMe(m_caster, threatToAdd, m_spellInfo);
        // for negative spells threat gets distributed among affected targets
        else
        {
            if (!target->CanHaveThreatList())
                continue;

            target->GetThreatManager().AddThreat(m_caster, threatToAdd, m_spellInfo, true);
        }
    }
    TC_LOG_DEBUG("spells", "Spell %u, added an additional %f threat for %s %u target(s)", m_spellInfo->Id, threat, m_spellInfo->_IsPositiveSpell() ? "assisting" : "harming", uint32(m_UniqueTargetInfo.size()));
}

void Spell::HandleEffects(Unit* pUnitTarget, Item* pItemTarget, GameObject* pGOTarget, uint32 i, SpellEffectHandleMode mode)
{

    effectHandleMode = mode;
    unitTarget = pUnitTarget;
    itemTarget = pItemTarget;
    gameObjTarget = pGOTarget;
    destTarget = &m_destTargets[i]._position;

    uint8 eff = m_spellInfo->Effects[i].Effect;

    TC_LOG_DEBUG("spells", "Spell: %u Effect : %u", m_spellInfo->Id, eff);

    // we do not need DamageMultiplier here.
    damage = CalculateDamage(i, NULL);

    bool preventDefault = CallScriptEffectHandlers((SpellEffIndex)i, mode);

    if (!preventDefault && eff < TOTAL_SPELL_EFFECTS)
    {
        (this->*SpellEffects[eff])((SpellEffIndex)i);
    }
}

SpellCastResult Spell::CheckCast(bool strict)
{
    // check death state
    if (!m_caster->IsAlive() && !m_spellInfo->HasAttribute(SPELL_ATTR0_PASSIVE) && !(m_spellInfo->HasAttribute(SPELL_ATTR0_CASTABLE_WHILE_DEAD) || (IsTriggered() && !m_triggeredByAuraSpell)))
        return SPELL_FAILED_CASTER_DEAD;

    // Ambrius: It may seem counterintuitive not to skip cooldown checking for spells with TRIGGERD_IGNORE_SPELL_AND_CATEGORY_CD, here is how it works afaik:
    //   For spells with this flag no cooldown is set in Spell:SendSpellCooldown. So these will not be on cooldown if they havent been cast without the flag before.
    //   Furthermore, we cannot simply skip cooldown checking for those spells because unfortunately there exist hacks using TRIGGERD_IGNORE_SPELL_AND_CATEGORY_CD 
    //      for spells that should in fact *have* a cooldown (e.g. 71519). The cooldown is then manually added afterwards.

    // check cooldowns to prevent cheating
    if (!m_spellInfo->IsPassive())
    {
        if (m_caster->GetTypeId() == TYPEID_PLAYER)
        {
            //can cast triggered (by aura only?) spells while have this flag
            if (!(_triggeredCastFlags & TRIGGERED_IGNORE_CASTER_AURASTATE) && m_caster->ToPlayer()->HasFlag(PLAYER_FLAGS, PLAYER_ALLOW_ONLY_ABILITY))
                return SPELL_FAILED_SPELL_IN_PROGRESS;

            // check if we are using a potion in combat for the 2nd+ time. Cooldown is added only after caster gets out of combat
            if (m_caster->ToPlayer()->GetLastPotionId() && m_CastItem && (m_CastItem->IsPotion() || m_spellInfo->IsCooldownStartedOnEvent()))
                return SPELL_FAILED_NOT_READY;
        }

        bool ignoreCooldown = false;
        if (Creature* npc = m_caster->ToCreature())
            ignoreCooldown = npc->IsDungeonBoss() || npc->isWorldBoss();

        if (!ignoreCooldown && !m_caster->GetSpellHistory()->IsReady(m_spellInfo))
        {
            if (m_triggeredByAuraSpell)
                return SPELL_FAILED_DONT_REPORT;
            else
                return SPELL_FAILED_NOT_READY;
        }
    }

    if (m_spellInfo->HasAttribute(SPELL_ATTR7_IS_CHEAT_SPELL) && !m_caster->HasFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_ALLOW_CHEAT_SPELLS))
    {
        m_customError = SPELL_CUSTOM_ERROR_GM_ONLY;
        return SPELL_FAILED_CUSTOM_ERROR;
    }

    // Check global cooldown
    if (strict && !(_triggeredCastFlags & TRIGGERED_IGNORE_GCD) && HasGlobalCooldown() && !(m_spellInfo->Attributes & SPELL_ATTR0_DISABLED_WHILE_ACTIVE))
        return SPELL_FAILED_NOT_READY;

    // only triggered spells can be processed an ended battleground
    if (!IsTriggered() && m_caster->GetTypeId() == TYPEID_PLAYER)
        if (Battleground* bg = m_caster->ToPlayer()->GetBattleground())
            if (bg->GetStatus() == STATUS_WAIT_LEAVE)
                return SPELL_FAILED_DONT_REPORT;

    // Deathgrip shouldn't be useable while jumping or falling
    if (m_caster->GetTypeId() == TYPEID_PLAYER && m_spellInfo->HasAttribute(SPELL_ATTR6_DEATHGRIP))
        if (m_caster->HasUnitMovementFlag(MOVEMENTFLAG_FALLING))
            return SPELL_FAILED_MOVING;

    // Hack: Do not allow cast of Divine Intervention in battleground preparation
    if (m_spellInfo->Id == 19752)
        if (Battleground* bg = m_caster->ToPlayer()->GetBattleground())
            if (bg->GetStatus() == STATUS_WAIT_JOIN)
                return SPELL_FAILED_DONT_REPORT;

    // Mind Control Check
    if (Spell* curSpell = m_caster->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        if ((curSpell->GetSpellInfo()->AttributesEx & SPELL_ATTR1_CHANNELED_1) && curSpell->GetSpellInfo()->HasAura(SPELL_AURA_MOD_POSSESS))
            if (!m_spellInfo->HasAura(SPELL_AURA_MOD_MELEE_HASTE) || (curSpell->GetSpellInfo() == m_spellInfo && strict))
                return SPELL_FAILED_SPELL_IN_PROGRESS;

    if (m_caster->GetTypeId() == TYPEID_PLAYER && VMAP::VMapFactory::createOrGetVMapManager()->isLineOfSightCalcEnabled())
    {
        if (m_spellInfo->HasAttribute(SPELL_ATTR0_OUTDOORS_ONLY) &&
                !m_caster->GetMap()->IsOutdoors(m_caster->GetPositionX(), m_caster->GetPositionY(), m_caster->GetPositionZ()))
            return SPELL_FAILED_ONLY_OUTDOORS;

        if (m_spellInfo->HasAttribute(SPELL_ATTR0_INDOORS_ONLY) &&
                m_caster->GetMap()->IsOutdoors(m_caster->GetPositionX(), m_caster->GetPositionY(), m_caster->GetPositionZ()))
            return SPELL_FAILED_ONLY_INDOORS;
    }

    // only check at first call, Stealth auras are already removed at second call
    // for now, ignore triggered spells
    if (strict && !(_triggeredCastFlags & TRIGGERED_IGNORE_SHAPESHIFT))
    {
        bool checkForm = true;
        // Ignore form req aura
        Unit::AuraEffectList const& ignore = m_caster->GetAuraEffectsByType(SPELL_AURA_MOD_IGNORE_SHAPESHIFT);
        for (Unit::AuraEffectList::const_iterator i = ignore.begin(); i != ignore.end(); ++i)
        {
            if (!(*i)->IsAffectedOnSpell(m_spellInfo))
                continue;
            checkForm = false;
            break;
        }
        if (checkForm)
        {
            // Cannot be used in this stance/form
            SpellCastResult shapeError = m_spellInfo->CheckShapeshift(m_caster->GetShapeshiftForm());
            if (shapeError != SPELL_CAST_OK)
                return shapeError;

            if (m_spellInfo->HasAttribute(SPELL_ATTR0_ONLY_STEALTHED) && !(m_caster->HasStealthAura()))
                return SPELL_FAILED_ONLY_STEALTHED;
        }
    }

    Unit::AuraEffectList const& blockSpells = m_caster->GetAuraEffectsByType(SPELL_AURA_BLOCK_SPELL_FAMILY);
    for (Unit::AuraEffectList::const_iterator blockItr = blockSpells.begin(); blockItr != blockSpells.end(); ++blockItr)
        if (uint32((*blockItr)->GetMiscValue()) == m_spellInfo->SpellFamilyName)
            return SPELL_FAILED_SPELL_UNAVAILABLE;

    bool reqCombat = true;
    Unit::AuraEffectList const& stateAuras = m_caster->GetAuraEffectsByType(SPELL_AURA_ABILITY_IGNORE_AURASTATE);
    for (Unit::AuraEffectList::const_iterator j = stateAuras.begin(); j != stateAuras.end(); ++j)
    {
        if ((*j)->IsAffectedOnSpell(m_spellInfo))
        {
            m_needComboPoints = false;
            if ((*j)->GetMiscValue() == 1)
            {
                reqCombat=false;
                break;
            }
        }
    }

    // caster state requirements
    // not for triggered spells (needed by execute)
    if (!(_triggeredCastFlags & TRIGGERED_IGNORE_CASTER_AURASTATE))
    {
        if (m_spellInfo->CasterAuraState && !m_caster->HasAuraState(AuraStateType(m_spellInfo->CasterAuraState), m_spellInfo, m_caster))
            return SPELL_FAILED_CASTER_AURASTATE;
        if (m_spellInfo->CasterAuraStateNot && m_caster->HasAuraState(AuraStateType(m_spellInfo->CasterAuraStateNot), m_spellInfo, m_caster))
            return SPELL_FAILED_CASTER_AURASTATE;

        // Note: spell 62473 requres casterAuraSpell = triggering spell
        if (m_spellInfo->CasterAuraSpell && !m_caster->HasAura(sSpellMgr->GetSpellIdForDifficulty(m_spellInfo->CasterAuraSpell, m_caster)))
            return SPELL_FAILED_CASTER_AURASTATE;
        if (m_spellInfo->ExcludeCasterAuraSpell && m_caster->HasAura(sSpellMgr->GetSpellIdForDifficulty(m_spellInfo->ExcludeCasterAuraSpell, m_caster)))
            return SPELL_FAILED_CASTER_AURASTATE;

        if (reqCombat && m_caster->IsInCombat() && !m_spellInfo->CanBeUsedInCombat())
            return SPELL_FAILED_AFFECTING_COMBAT;
    }

    // cancel autorepeat spells if cast start when moving
    // (not wand currently autorepeat cast delayed to moving stop anyway in spell update code)
    if (m_caster->GetTypeId() == TYPEID_PLAYER && m_caster->ToPlayer()->isMoving() && (!m_caster->IsCharmed() || !m_caster->GetCharmerGUID().IsCreature()))
    {
        // skip stuck spell to allow use it in falling case and apply spell limitations at movement
        if ((!m_caster->HasUnitMovementFlag(MOVEMENTFLAG_FALLING_FAR) || m_spellInfo->Effects[0].Effect != SPELL_EFFECT_STUCK) &&
            (IsAutoRepeat() || (m_spellInfo->AuraInterruptFlags & AURA_INTERRUPT_FLAG_NOT_SEATED) != 0))
            return SPELL_FAILED_MOVING;
    }

    // Check vehicle flags
    if (!(_triggeredCastFlags & TRIGGERED_IGNORE_CASTER_MOUNTED_OR_ON_VEHICLE))
    {
        SpellCastResult vehicleCheck = m_spellInfo->CheckVehicle(m_caster);
        if (vehicleCheck != SPELL_CAST_OK)
            return vehicleCheck;
    }

    // check spell cast conditions from database
    {
        ConditionSourceInfo condInfo = ConditionSourceInfo(m_caster, m_targets.GetObjectTarget());
        if (!sConditionMgr->IsObjectMeetingNotGroupedConditions(CONDITION_SOURCE_TYPE_SPELL, m_spellInfo->Id, condInfo))
        {
            // mLastFailedCondition can be NULL if there was an error processing the condition in Condition::Meets (i.e. wrong data for ConditionTarget or others)
            if (condInfo.mLastFailedCondition && condInfo.mLastFailedCondition->ErrorType)
            {
                if (condInfo.mLastFailedCondition->ErrorType == SPELL_FAILED_CUSTOM_ERROR)
                    m_customError = SpellCustomErrors(condInfo.mLastFailedCondition->ErrorTextId);
                return SpellCastResult(condInfo.mLastFailedCondition->ErrorType);
            }

            if (_triggeredCastFlags & TRIGGERED_DONT_REPORT_CAST_ERROR)
                return SPELL_FAILED_DONT_REPORT;

            if (!condInfo.mLastFailedCondition || !condInfo.mLastFailedCondition->ConditionTarget)
                return SPELL_FAILED_CASTER_AURASTATE;
            return SPELL_FAILED_BAD_TARGETS;
        }
    }

    // Don't check explicit target for passive spells (workaround) (check should be skipped only for learn case)
    // those spells may have incorrect target entries or not filled at all (for example 15332)
    // such spells when learned are not targeting anyone using targeting system, they should apply directly to caster instead
    // also, such casts shouldn't be sent to client
    if (!(m_spellInfo->HasAttribute(SPELL_ATTR0_PASSIVE) && (!m_targets.GetUnitTarget() || m_targets.GetUnitTarget() == m_caster)))
    {
        // Check explicit target for m_originalCaster - todo: get rid of such workarounds
        SpellCastResult castResult = m_spellInfo->CheckExplicitTarget(m_originalCaster ? m_originalCaster : m_caster, m_targets.GetObjectTarget(), m_targets.GetItemTarget());
        if (castResult != SPELL_CAST_OK)
            return castResult;
    }

    if (Unit* target = m_targets.GetUnitTarget())
    {
        SpellCastResult castResult = m_spellInfo->CheckTarget(m_caster, target, false);
        if (castResult != SPELL_CAST_OK)
            return castResult;

        if (target != m_caster)
        {
            // Must be behind the target
            if (m_spellInfo->HasAttribute(SPELL_ATTR0_CU_REQ_CASTER_BEHIND_TARGET) && target->HasInArc(static_cast<float>(M_PI), m_caster))
                return SPELL_FAILED_NOT_BEHIND;

            // Target must be facing you
            if (m_spellInfo->HasAttribute(SPELL_ATTR0_CU_REQ_TARGET_FACING_CASTER) && !target->HasInArc(static_cast<float>(M_PI), m_caster))
                return SPELL_FAILED_NOT_INFRONT;

            if (!m_softRefresh   // do not check if we recast the spell to refresh the aura, the LoS check is already performed by the triggering spell
                && m_caster->GetEntry() != WORLD_TRIGGER) // Ignore LOS for gameobjects casts (wrongly cast by a trigger)
            {
                WorldObject* losTarget = m_caster;
                if (IsTriggered() && m_triggeredByAuraSpell)
                    if (DynamicObject* dynObj = m_caster->GetDynObject(m_triggeredByAuraSpell->Id))
                        losTarget = dynObj;
            
                if (!m_spellInfo->HasAttribute(SPELL_ATTR2_CAN_TARGET_NOT_IN_LOS) && !DisableMgr::IsDisabledFor(DISABLE_TYPE_SPELL, m_spellInfo->Id, NULL, SPELL_DISABLE_LOS) && !m_spellInfo->HasAttribute(SPELL_ATTR5_UNK26) && !target->IsWithinLOSInMap(losTarget))
                    return SPELL_FAILED_LINE_OF_SIGHT;
            }
        }
    }

    // Check for line of sight for spells with dest
    if (m_targets.HasDst())
    {
        float x, y, z;
        m_targets.GetDstPos()->GetPosition(x, y, z);

        if (!m_spellInfo->HasAttribute(SPELL_ATTR2_CAN_TARGET_NOT_IN_LOS) && !DisableMgr::IsDisabledFor(DISABLE_TYPE_SPELL, m_spellInfo->Id, NULL, SPELL_DISABLE_LOS) && !m_spellInfo->HasAttribute(SPELL_ATTR5_UNK26) && !m_caster->IsWithinLOS(x, y, z))
            return SPELL_FAILED_LINE_OF_SIGHT;
    }

    // check pet presence
    for (int j = 0; j < MAX_SPELL_EFFECTS; ++j)
    {
        if (m_spellInfo->Effects[j].TargetA.GetTarget() == TARGET_UNIT_PET)
        {
            if (Unit* pet = m_caster->GetGuardianPet())
            {
                if (!m_spellInfo->HasAttribute(SPELL_ATTR2_CAN_TARGET_NOT_IN_LOS) && !DisableMgr::IsDisabledFor(DISABLE_TYPE_SPELL, m_spellInfo->Id, NULL, SPELL_DISABLE_LOS) && !m_caster->IsWithinLOSInMap(pet))
                    return SPELL_FAILED_LINE_OF_SIGHT;
            }
            else
            {
                if (m_triggeredByAuraSpell)              // not report pet not existence for triggered spells
                    return SPELL_FAILED_DONT_REPORT;
                else
                    return SPELL_FAILED_NO_PET;
            }
            break;
        }
    }

    // Spell cast only in battleground
    if (m_spellInfo->HasAttribute(SPELL_ATTR3_BATTLEGROUND) &&  m_caster->GetTypeId() == TYPEID_PLAYER)
        if (!m_caster->ToPlayer()->InBattleground())
            return SPELL_FAILED_ONLY_BATTLEGROUNDS;

    // do not allow spells to be cast in arenas
    // - with greater than 10 min CD without SPELL_ATTR4_USABLE_IN_ARENA flag
    // - with SPELL_ATTR4_NOT_USABLE_IN_ARENA flag
    if (m_spellInfo->HasAttribute(SPELL_ATTR4_NOT_USABLE_IN_ARENA) ||
        (m_spellInfo->GetRecoveryTime() > 10 * MINUTE * IN_MILLISECONDS && !m_spellInfo->HasAttribute(SPELL_ATTR4_USABLE_IN_ARENA)))
        if (MapEntry const* mapEntry = sMapStore.LookupEntry(m_caster->GetMapId()))
            if (mapEntry->IsBattleArena())
                return SPELL_FAILED_NOT_IN_ARENA;

    // zone check
    if (m_caster->GetTypeId() == TYPEID_UNIT || !m_caster->ToPlayer()->IsGameMaster())
    {
        uint32 zone, area;
        m_caster->GetZoneAndAreaId(zone, area);

        SpellCastResult locRes = m_spellInfo->CheckLocation(m_caster->GetMapId(), zone, area, m_caster->ToPlayer());
        if (locRes != SPELL_CAST_OK)
            return locRes;
    }

    // not let players cast spells at mount (and let do it to creatures)
    if (m_caster->IsMounted() && m_caster->GetTypeId() == TYPEID_PLAYER && !(_triggeredCastFlags & TRIGGERED_IGNORE_CASTER_MOUNTED_OR_ON_VEHICLE) &&
        !m_spellInfo->IsPassive() && !m_spellInfo->HasAttribute(SPELL_ATTR0_CASTABLE_WHILE_MOUNTED))
    {
        if (m_caster->IsInFlight())
            return SPELL_FAILED_NOT_ON_TAXI;
        else
            return SPELL_FAILED_NOT_MOUNTED;
    }

    // check spell focus object
    if (m_spellInfo->RequiresSpellFocus)
    {
        focusObject = SearchSpellFocus();
        if (!focusObject)
            return SPELL_FAILED_REQUIRES_SPELL_FOCUS;
    }

    SpellCastResult castResult = SPELL_CAST_OK;

    // always (except passive spells) check items (only player related checks)
    if (!m_spellInfo->IsPassive())
    {
        castResult = CheckItems();
        if (castResult != SPELL_CAST_OK)
            return castResult;
    }

    // Triggered spells also have range check
    /// @todo determine if there is some flag to enable/disable the check
    castResult = CheckRange(strict);
    if (castResult != SPELL_CAST_OK)
        return castResult;

    if (!(_triggeredCastFlags & TRIGGERED_IGNORE_POWER_AND_REAGENT_COST))
    {
        castResult = CheckPower();
        if (castResult != SPELL_CAST_OK)
            return castResult;
    }

    if (!(_triggeredCastFlags & TRIGGERED_IGNORE_CASTER_AURAS))
    {
        castResult = CheckCasterAuras();
        if (castResult != SPELL_CAST_OK)
            return castResult;
    }

    if (!(*this <<= Query::CanCast))
        return SPELL_FAILED_BAD_TARGETS;

    // script hook
    castResult = CallScriptCheckCastHandlers();
    if (castResult != SPELL_CAST_OK)
        return castResult;

    bool hasDispellableAura = false;
    bool hasNonDispelEffect = false;
    uint32 dispelMask = 0;
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        if (m_spellInfo->Effects[i].Effect == SPELL_EFFECT_DISPEL)
        {
            if (m_spellInfo->Effects[i].IsTargetingArea() || m_spellInfo->HasAttribute(SPELL_ATTR1_MELEE_COMBAT_START))
            {
                hasDispellableAura = true;
                break;
            }

            dispelMask |= SpellInfo::GetDispelMask(DispelType(m_spellInfo->Effects[i].MiscValue));
        }
        else if (m_spellInfo->Effects[i].IsEffect())
        {
            hasNonDispelEffect = true;
            break;
        }

    if (!hasNonDispelEffect && !hasDispellableAura && dispelMask && !IsTriggered())
    {
        if (Unit* target = m_targets.GetUnitTarget())
        {
            DispelChargesList dispelList;
            target->GetDispellableAuraList(m_caster, dispelMask, dispelList);
            if (dispelList.empty())
                return SPELL_FAILED_NOTHING_TO_DISPEL;
        }
    }

    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        // for effects of spells that have only one target
        switch (m_spellInfo->Effects[i].Effect)
        {
            case SPELL_EFFECT_LEARN_SPELL:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    return SPELL_FAILED_BAD_TARGETS;

                if (m_spellInfo->Effects[i].TargetA.GetTarget() != TARGET_UNIT_PET)
                    break;

                Pet* pet = m_caster->ToPlayer()->GetPet();

                if (!pet)
                    return SPELL_FAILED_NO_PET;

                SpellInfo const* learn_spellproto = sSpellMgr->GetSpellInfo(m_spellInfo->Effects[i].TriggerSpell);

                if (!learn_spellproto)
                    return SPELL_FAILED_NOT_KNOWN;

                if (m_spellInfo->SpellLevel > pet->getLevel())
                    return SPELL_FAILED_LOWLEVEL;

                break;
            }
            case SPELL_EFFECT_LEARN_PET_SPELL:
            {
                // check target only for unit target case
                if (Unit* unitTarget = m_targets.GetUnitTarget())
                {
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                        return SPELL_FAILED_BAD_TARGETS;

                    Pet* pet = unitTarget->ToPet();
                    if (!pet || pet->GetOwner() != m_caster)
                        return SPELL_FAILED_BAD_TARGETS;

                    SpellInfo const* learn_spellproto = sSpellMgr->GetSpellInfo(m_spellInfo->Effects[i].TriggerSpell);

                    if (!learn_spellproto)
                        return SPELL_FAILED_NOT_KNOWN;

                    if (m_spellInfo->SpellLevel > pet->getLevel())
                        return SPELL_FAILED_LOWLEVEL;
                }
                break;
            }
            case SPELL_EFFECT_APPLY_GLYPH:
            {
                uint32 glyphId = m_spellInfo->Effects[i].MiscValue;
                if (GlyphPropertiesEntry const* gp = sGlyphPropertiesStore.LookupEntry(glyphId))
                    if (m_caster->HasAura(gp->SpellId))
                        return SPELL_FAILED_UNIQUE_GLYPH;
                break;
            }
            case SPELL_EFFECT_FEED_PET:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    return SPELL_FAILED_BAD_TARGETS;

                Item* foodItem = m_targets.GetItemTarget();
                if (!foodItem)
                    return SPELL_FAILED_BAD_TARGETS;

                Pet* pet = m_caster->ToPlayer()->GetPet();

                if (!pet)
                    return SPELL_FAILED_NO_PET;

                if (!pet->HaveInDiet(foodItem->GetTemplate()))
                    return SPELL_FAILED_WRONG_PET_FOOD;

                if (!pet->GetCurrentFoodBenefitLevel(foodItem->GetTemplate()->ItemLevel))
                    return SPELL_FAILED_FOOD_LOWLEVEL;

                if (m_caster->IsInCombat() || pet->IsInCombat())
                    return SPELL_FAILED_AFFECTING_COMBAT;

                break;
            }
            case SPELL_EFFECT_POWER_BURN:
            case SPELL_EFFECT_POWER_DRAIN:
            {
                // Can be area effect, Check only for players and not check if target - caster (spell can have multiply drain/burn effects)
                if (m_caster->GetTypeId() == TYPEID_PLAYER)
                    if (Unit* target = m_targets.GetUnitTarget())
                        if (target != m_caster && target->getPowerType() != Powers(m_spellInfo->Effects[i].MiscValue))
                            return SPELL_FAILED_BAD_TARGETS;
                break;
            }
            case SPELL_EFFECT_CHARGE:
            {
                if (m_spellInfo->SpellFamilyName == SPELLFAMILY_WARRIOR)
                {
                    // Warbringer - can't be handled in proc system - should be done before checkcast root check and charge effect process
                    if (strict && m_caster->IsScriptOverriden(m_spellInfo, 6953))
                        m_caster->RemoveMovementImpairingAuras();
                }

                if (m_caster->HasUnitState(UNIT_STATE_ROOT))
                    return SPELL_FAILED_ROOTED;

                if (GetSpellInfo()->NeedsExplicitUnitTarget())
                {
                    Unit* target = m_targets.GetUnitTarget();
                    if (!target)
                        return SPELL_FAILED_DONT_REPORT;

                    float objSize = m_caster->GetMeleeRange(target);
                    float range = m_spellInfo->GetMaxRange(true, m_caster, this) * 1.5f + objSize; // can't be overly strict

                    preGeneratedPath = std::make_unique<PathGenerator>(m_caster);

                    preGeneratedPath->SetPathLengthLimit(range);

                    float x, y, z;
                    uint32 predictedTime;
                    if (m_spellInfo->IsPositive())
                        predictedTime = 500;
                    else if (target->IsOfType<Player>())
                        predictedTime = 100; // compensate latency
                    else
                        predictedTime = 0;
                    target->CalculateExpectedPosition(predictedTime).GetPosition(x, y, z);
                    if (auto transport = preGeneratedPath->GetTransport())
                        transport->CalculatePassengerOffset(x, y, z);

                    // first try with raycast, if it fails fall back to normal path
                    bool result = false;
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    {
                        result = preGeneratedPath->CalculatePath(x, y, z + target->GetObjectSize(), false, true);
                        if (preGeneratedPath->GetPathType() & PATHFIND_SHORT)
                            return SPELL_FAILED_OUT_OF_RANGE;
                    }
                    if (!result || preGeneratedPath->GetPathType() & PATHFIND_NOPATH)
                    {
                        result = preGeneratedPath->CalculatePath(x, y, z + target->GetObjectSize(), false, false);
                        if (!result || preGeneratedPath->GetPathType() & PATHFIND_SHORT || preGeneratedPath->GetPathType() & PATHFIND_NOPATH)
                            return SPELL_FAILED_NOPATH;
                    }

                    objSize -= (preGeneratedPath->GetActualEndPosition() - G3D::Vector3(x, y, z)).length() + 4.0f / 3.0f;
                    if (objSize > 0.f)
                        preGeneratedPath->ReducePathLenghtByDist(objSize); // move back
                }
                break;
            }
            case SPELL_EFFECT_SKINNING:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER || !m_targets.GetUnitTarget() || m_targets.GetUnitTarget()->GetTypeId() != TYPEID_UNIT)
                    return SPELL_FAILED_BAD_TARGETS;

                if (!(m_targets.GetUnitTarget()->GetUInt32Value(UNIT_FIELD_FLAGS) & UNIT_FLAG_SKINNABLE))
                    return SPELL_FAILED_TARGET_UNSKINNABLE;

                Creature* creature = m_targets.GetUnitTarget()->ToCreature();
                if (creature->GetCreatureType() != CREATURE_TYPE_CRITTER && !creature->loot.isLooted())
                    return SPELL_FAILED_TARGET_NOT_LOOTED;

                uint32 skill = creature->GetCreatureTemplate()->GetRequiredLootSkill();

                int32 skillValue = m_caster->ToPlayer()->GetSkillValue(skill);
                int32 TargetLevel = m_targets.GetUnitTarget()->getLevel();
                int32 ReqValue = (skillValue < 100 ? (TargetLevel-10) * 10 : TargetLevel * 5);
                if (ReqValue > skillValue)
                    return SPELL_FAILED_LOW_CASTLEVEL;

                // chance for fail at orange skinning attempt
                if ((m_selfContainer && (*m_selfContainer) == this) &&
                    skillValue < sWorld->GetConfigMaxSkillValue() &&
                    (ReqValue < 0 ? 0 : ReqValue) > irand(skillValue - 25, skillValue + 37))
                    return SPELL_FAILED_TRY_AGAIN;

                break;
            }
            case SPELL_EFFECT_OPEN_LOCK:
            {
                if (m_spellInfo->Effects[i].TargetA.GetTarget() != TARGET_GAMEOBJECT_TARGET &&
                    m_spellInfo->Effects[i].TargetA.GetTarget() != TARGET_GAMEOBJECT_ITEM_TARGET)
                    break;

                if (m_caster->GetTypeId() != TYPEID_PLAYER  // only players can open locks, gather etc.
                    // we need a go target in case of TARGET_GAMEOBJECT_TARGET
                    || (m_spellInfo->Effects[i].TargetA.GetTarget() == TARGET_GAMEOBJECT_TARGET && !m_targets.GetGOTarget()))
                    return SPELL_FAILED_BAD_TARGETS;

                Item* pTempItem = NULL;
                if (m_targets.GetTargetMask() & TARGET_FLAG_TRADE_ITEM)
                {
                    if (TradeData* pTrade = m_caster->ToPlayer()->GetTradeData())
                        pTempItem = pTrade->GetTraderData()->GetItem(TradeSlots(m_targets.GetItemTargetGUID().GetRawValue()));  // at this point item target guid contains the trade slot
                }
                else if (m_targets.GetTargetMask() & TARGET_FLAG_ITEM)
                    pTempItem = m_caster->ToPlayer()->GetItemByGuid(m_targets.GetItemTargetGUID());

                // we need a go target, or an openable item target in case of TARGET_GAMEOBJECT_ITEM_TARGET
                if (m_spellInfo->Effects[i].TargetA.GetTarget() == TARGET_GAMEOBJECT_ITEM_TARGET &&
                    !m_targets.GetGOTarget() &&
                    (!pTempItem || !pTempItem->GetTemplate()->LockID || !pTempItem->IsLocked()))
                    return SPELL_FAILED_BAD_TARGETS;

                if (m_spellInfo->Id != 1842 || (m_targets.GetGOTarget() &&
                    m_targets.GetGOTarget()->GetGOInfo()->type != GAMEOBJECT_TYPE_TRAP))
                    if (m_caster->ToPlayer()->InBattleground() && // In Battleground players can use only flags and banners
                        !m_caster->ToPlayer()->CanUseBattlegroundObject(m_targets.GetGOTarget()))
                        return SPELL_FAILED_TRY_AGAIN;

                if (m_targets.GetGOTarget() &&
                    m_targets.GetGOTarget()->GetGOInfo()->entry == 179697) // Arena Treasure Chest
                    if (!m_caster->ToPlayer()->CanUseOpenWorldPvPObject(m_targets.GetGOTarget()))
                        return SPELL_FAILED_TRY_AGAIN;

                // get the lock entry
                uint32 lockId = 0;
                if (GameObject* go = m_targets.GetGOTarget())
                {
                    lockId = go->GetGOInfo()->GetLockId();
                    if (!lockId)
                        return SPELL_FAILED_BAD_TARGETS;
                }
                else if (Item* itm = m_targets.GetItemTarget())
                    lockId = itm->GetTemplate()->LockID;

                SkillType skillId = SKILL_NONE;
                int32 reqSkillValue = 0;
                int32 skillValue = 0;

                // check lock compatibility
                SpellCastResult res = CanOpenLock(i, lockId, skillId, reqSkillValue, skillValue);
                if (res != SPELL_CAST_OK)
                    return res;

                break;
            }
            case SPELL_EFFECT_RESURRECT_PET:
            {
                Creature* pet = m_caster->GetGuardianPet();

                if (pet && pet->IsAlive())
                    return SPELL_FAILED_ALREADY_HAVE_SUMMON;

                if (!pet)
                if (auto playerCaster = m_caster->ToPlayer())
                    if (!playerCaster->GetPetCache().HasDeadHunterPet())
                        return SPELL_FAILED_TARGET_NOT_DEAD;

                break;
            }
            // This is generic summon effect
            case SPELL_EFFECT_SUMMON:
            {
                SummonPropertiesEntry const* SummonProperties = sSummonPropertiesStore.LookupEntry(m_spellInfo->Effects[i].MiscValueB);
                if (!SummonProperties)
                    break;
                switch (SummonProperties->Category)
                {
                    case SUMMON_CATEGORY_PET:
                        if (!m_spellInfo->HasAttribute(SPELL_ATTR1_DISMISS_PET) && m_caster->GetPetGUID())
                            if (m_spellInfo->Id != 70907 && m_spellInfo->Id != 70908 && m_spellInfo->Id != 34433) // NO check for Mageelemental or Shadowfiend
                                return SPELL_FAILED_ALREADY_HAVE_SUMMON;
                    // intentional missing break, check both GetPetGUID() and GetCharmGUID for SUMMON_CATEGORY_PET
                    case SUMMON_CATEGORY_PUPPET:
                        if (m_caster->GetCharmGUID())
                            return SPELL_FAILED_ALREADY_HAVE_CHARM;
                        break;
                }
                break;
            }
            case SPELL_EFFECT_CREATE_TAMED_PET:
            {
                if (m_targets.GetUnitTarget())
                {
                    if (m_targets.GetUnitTarget()->GetTypeId() != TYPEID_PLAYER)
                        return SPELL_FAILED_BAD_TARGETS;
                    if (!m_spellInfo->HasAttribute(SPELL_ATTR1_DISMISS_PET) && m_targets.GetUnitTarget()->GetPetGUID())
                        return SPELL_FAILED_ALREADY_HAVE_SUMMON;
                }
                break;
            }
            case SPELL_EFFECT_SUMMON_PET:
            {
                if (m_caster->GetPetGUID())                  //let warlock do a replacement summon
                {
                    if (m_caster->GetTypeId() == TYPEID_PLAYER)
                    {
                        if (strict)                         //starting cast, trigger pet stun (cast by pet so it doesn't attack player)
                            if (Pet* pet = m_caster->ToPlayer()->GetPet())
                                pet->CastSpell(pet, 32752, true, NULL, NULL, pet->GetGUID());
                    }
                    else if (!m_spellInfo->HasAttribute(SPELL_ATTR1_DISMISS_PET))
                        return SPELL_FAILED_ALREADY_HAVE_SUMMON;
                }

                if (m_caster->GetCharmGUID())
                    return SPELL_FAILED_ALREADY_HAVE_CHARM;
                if (auto playerCaster = m_caster->ToPlayer())
                    if (playerCaster->getClass() == CLASS_HUNTER)
                        if (playerCaster->GetPetCache().HasDeadHunterPet())
                            return SPELL_FAILED_TARGETS_DEAD;
                break;
            }
            case SPELL_EFFECT_SUMMON_PLAYER:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    return SPELL_FAILED_BAD_TARGETS;
                if (!m_caster->GetTarget())
                    return SPELL_FAILED_BAD_TARGETS;

                Player* target = ObjectAccessor::FindPlayer(m_caster->ToPlayer()->GetTarget());
                if (!target || m_caster->ToPlayer() == target || (!target->IsInSameRaidWith(m_caster->ToPlayer()) && m_spellInfo->Id != 48955)) // refer-a-friend spell
                    return SPELL_FAILED_BAD_TARGETS;

                // check if our map is dungeon
                MapEntry const* map = sMapStore.LookupEntry(m_caster->GetMapId());
                if (map->IsDungeon())
                {
                    uint32 mapId = m_caster->GetMap()->GetId();
                    Difficulty difficulty = m_caster->GetMap()->GetDifficulty();
                    if (map->IsRaid())
                        if (InstancePlayerBind* targetBind = target->GetBoundInstance(mapId, difficulty))
                            if (InstancePlayerBind* casterBind = m_caster->ToPlayer()->GetBoundInstance(mapId, difficulty))
                                if (targetBind->perm && targetBind->save != casterBind->save)
                                    return SPELL_FAILED_TARGET_LOCKED_TO_RAID_INSTANCE;

                    InstanceTemplate const* instance = sObjectMgr->GetInstanceTemplate(mapId);
                    if (!instance)
                        return SPELL_FAILED_TARGET_NOT_IN_INSTANCE;
                    if (!target->Satisfy(sObjectMgr->GetAccessRequirement(mapId, difficulty), mapId))
                        return SPELL_FAILED_BAD_TARGETS;
                }
                break;
            }
            // RETURN HERE
            case SPELL_EFFECT_SUMMON_RAF_FRIEND:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    return SPELL_FAILED_BAD_TARGETS;

                Player* playerCaster = m_caster->ToPlayer();
                    //
                if (!(playerCaster->GetTarget()))
                    return SPELL_FAILED_BAD_TARGETS;

                Player* target = playerCaster->GetSelectedPlayer();

                if (!target ||
                    !(target->GetSession()->GetRecruiterId() == playerCaster->GetSession()->GetAccountId() || target->GetSession()->GetAccountId() == playerCaster->GetSession()->GetRecruiterId()))
                    return SPELL_FAILED_BAD_TARGETS;

                break;
            }
            case SPELL_EFFECT_TRANS_DOOR:
            {
                // Prevent using of Ritual of Summoning in Battlegrounds and Arenas
                if (m_spellInfo->Id == 698 && m_caster->GetTypeId() == TYPEID_PLAYER && m_caster->ToPlayer()->InBattleground())
                    return SPELL_FAILED_NOT_HERE;
                break;
            }
            case SPELL_EFFECT_LEAP:
            case SPELL_EFFECT_TELEPORT_UNITS_FACE_CASTER:
            {
              //Do not allow to cast it before BG starts.
                if (m_caster->GetTypeId() == TYPEID_PLAYER)
                    if (Battleground const* bg = m_caster->ToPlayer()->GetBattleground())
                        if (bg->GetStatus() != STATUS_IN_PROGRESS)
                            return SPELL_FAILED_TRY_AGAIN;
                break;
            }
            case SPELL_EFFECT_STEAL_BENEFICIAL_BUFF:
            {
                if (m_targets.GetUnitTarget() == m_caster)
                    return SPELL_FAILED_BAD_TARGETS;
                break;
            }
            case SPELL_EFFECT_LEAP_BACK:
            {
                if (m_caster->HasUnitState(UNIT_STATE_ROOT))
                {
                    if (m_caster->GetTypeId() == TYPEID_PLAYER)
                        return SPELL_FAILED_ROOTED;
                    else
                        return SPELL_FAILED_DONT_REPORT;
                }
                break;
            }
            case SPELL_EFFECT_TALENT_SPEC_SELECT:
                // can't change during already started arena/battleground
                if (m_caster->GetTypeId() == TYPEID_PLAYER)
                    if (Battleground const* bg = m_caster->ToPlayer()->GetBattleground())
                        if (bg->GetStatus() == STATUS_IN_PROGRESS)
                            return SPELL_FAILED_NOT_IN_BATTLEGROUND;
                break;
            default:
                break;
        }
    }

    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        switch (m_spellInfo->Effects[i].ApplyAuraName)
        {
            case SPELL_AURA_MOD_POSSESS_PET:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER)
                    return SPELL_FAILED_NO_PET;

                Pet* pet = m_caster->ToPlayer()->GetPet();
                if (!pet)
                    return SPELL_FAILED_NO_PET;

                if (pet->GetCharmerGUID())
                    return SPELL_FAILED_CHARMED;
                break;
            }
            case SPELL_AURA_MOD_POSSESS:
            case SPELL_AURA_MOD_CHARM:
            case SPELL_AURA_AOE_CHARM:
            {
                if (m_caster->GetCharmerGUID())
                    return SPELL_FAILED_CHARMED;

                if (m_spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_MOD_CHARM
                    || m_spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_MOD_POSSESS)
                {
                    if (!m_spellInfo->HasAttribute(SPELL_ATTR1_DISMISS_PET) && m_caster->GetPetGUID() && m_spellInfo->Id != 605) // if cast = mind control, then don't error
                        return SPELL_FAILED_ALREADY_HAVE_SUMMON;

                    if (m_caster->GetCharmGUID())
                        return SPELL_FAILED_ALREADY_HAVE_CHARM;
                }

                if (Unit* target = m_targets.GetUnitTarget())
                {
                    if (target->GetTypeId() == TYPEID_UNIT && target->IsVehicle())
                        return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                    if (target->IsMounted())
                        return SPELL_FAILED_CANT_BE_CHARMED;

                    if (target->GetCharmerGUID())
                        return SPELL_FAILED_CHARMED;

                    if (target->GetOwner() && target->GetOwner()->GetTypeId() == TYPEID_PLAYER)
                        return SPELL_FAILED_TARGET_IS_PLAYER_CONTROLLED;

                    int32 damage = CalculateDamage(i, target);
                    if (damage && int32(target->getLevel()) > damage)
                        return SPELL_FAILED_HIGHLEVEL;
                }

                break;
            }
            case SPELL_AURA_MOD_STEALTH:
            {
                //! This is a hack for Faerie Fire from NPC's - all id's are id's from Spell: Faerie Fire
                // added Perpetual Instability (49163) to this hack
                if (m_caster->HasAura(16498) || m_caster->HasAura(20656) || m_caster->HasAura(21670) || m_caster->HasAura(25602) ||
                    m_caster->HasAura(32129) || m_caster->HasAura(65863) || m_caster->HasAura(49163))
                {
                    return SPELL_FAILED_CANT_STEALTH;
                }

                break;
            }
            case SPELL_AURA_MOUNTED:
            {
                if (m_caster->IsInWater() && m_spellInfo->HasAura(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED))
                    return SPELL_FAILED_ONLY_ABOVEWATER;

                // Ignore map check if spell have AreaId. AreaId already checked and this prevent special mount spells
                bool allowMount = !m_caster->GetMap()->IsDungeon() || m_caster->GetMap()->IsBattlegroundOrArena();
                InstanceTemplate const* it = sObjectMgr->GetInstanceTemplate(m_caster->GetMapId());
                if (it)
                    allowMount = it->AllowMount;
                if (m_caster->GetTypeId() == TYPEID_PLAYER && !allowMount && !m_spellInfo->AreaGroupId)
                    return SPELL_FAILED_NO_MOUNTS_ALLOWED;

                Unit::AuraEffectList const& auras = m_caster->GetAuraEffectsByType(SPELL_AURA_TRANSFORM);
                if (!auras.empty())
                    for (Unit::AuraEffectList::const_iterator i = auras.begin(); i != auras.end(); i++)
                        if (!((*i)->GetSpellInfo()->Attributes & SPELL_ATTR0_CANT_CANCEL))
                            if (m_caster->IsInDisallowedMountForm())
                                return SPELL_FAILED_NOT_SHAPESHIFT;

                break;
            }
            case SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS:
            {
                if (!m_targets.GetUnitTarget())
                    return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                // can be cast at non-friendly unit or own pet/charm
                if (m_caster->IsFriendlyTo(m_targets.GetUnitTarget()))
                    return SPELL_FAILED_TARGET_FRIENDLY;

                break;
            }
            case SPELL_AURA_FLY:
            case SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED:
            {
                // not allow cast fly spells if not have req. skills  (all spells is self target)
                // allow always ghost flight spells
                if (m_originalCaster && m_originalCaster->GetTypeId() == TYPEID_PLAYER && m_originalCaster->IsAlive())
                {
                    Battlefield* Bf = sBattlefieldMgr->GetBattlefieldToZoneId(m_originalCaster->GetZoneId());
                    if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(m_originalCaster->GetAreaId()))
                        if (area->flags & AREA_FLAG_NO_FLY_ZONE  || (Bf && !Bf->CanFlyIn()))
                            return (_triggeredCastFlags & TRIGGERED_DONT_REPORT_CAST_ERROR) ? SPELL_FAILED_DONT_REPORT : SPELL_FAILED_NOT_HERE;
                }
                break;
            }
            case SPELL_AURA_PERIODIC_MANA_LEECH:
            {
                if (m_spellInfo->Effects[i].IsTargetingArea())
                    break;

                if (!m_targets.GetUnitTarget())
                    return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                if (m_caster->GetTypeId() != TYPEID_PLAYER || m_CastItem)
                    break;

                if (m_targets.GetUnitTarget()->getPowerType() != POWER_MANA)
                    return SPELL_FAILED_BAD_TARGETS;

                break;
            }
            default:
                break;
        }
    }

    // check trade slot case (last, for allow catch any another cast problems)
    if (m_targets.GetTargetMask() & TARGET_FLAG_TRADE_ITEM)
    {
        if (m_CastItem)
            return SPELL_FAILED_ITEM_ENCHANT_TRADE_WINDOW;

        if (m_caster->GetTypeId() != TYPEID_PLAYER)
            return SPELL_FAILED_NOT_TRADING;

        TradeData* my_trade = m_caster->ToPlayer()->GetTradeData();

        if (!my_trade)
            return SPELL_FAILED_NOT_TRADING;

        // Item target guid contains trade slot until m_targets.UpdateTradeSlotItem() is called
        TradeSlots slot = TradeSlots(m_targets.GetItemTargetGUID().GetRawValue());
        if (slot != TRADE_SLOT_NONTRADED)
            return SPELL_FAILED_BAD_TARGETS;

        if (!IsTriggered())
            if (my_trade->GetSpell())
                return SPELL_FAILED_ITEM_ALREADY_ENCHANTED;
    }

    // check if caster has at least 1 combo point for spells that require combo points
    if (m_needComboPoints)
        if (Player* plrCaster = m_caster->ToPlayer())
            if (!plrCaster->GetComboPoints())
                return SPELL_FAILED_NO_COMBO_POINTS;

    // all ok
    return SPELL_CAST_OK;
}

SpellCastResult Spell::CheckPetCast(Unit* target)
{
    if (m_caster->HasUnitState(UNIT_STATE_CASTING) && !(_triggeredCastFlags & TRIGGERED_IGNORE_CAST_IN_PROGRESS))              //prevent spellcast interruption by another spellcast
        return SPELL_FAILED_SPELL_IN_PROGRESS;

    // dead owner (pets still alive when owners ressed?)
    if (Unit* owner = m_caster->GetCharmerOrOwner())
        if (!owner->IsAlive() && !owner->IsGhouled())
            return SPELL_FAILED_CASTER_DEAD;

    if (!target && m_targets.GetUnitTarget())
        target = m_targets.GetUnitTarget();

    if (m_spellInfo->NeedsExplicitUnitTarget())
    {
        if (!target)
            return SPELL_FAILED_BAD_IMPLICIT_TARGETS;
        m_targets.SetUnitTarget(target);
    }

    // check cooldown
    if (Creature* creatureCaster = m_caster->ToCreature())
        if (!creatureCaster->GetSpellHistory()->IsReady(m_spellInfo))
            return SPELL_FAILED_NOT_READY;

    // this would be zero until ::prepare normally, we set it here (it gets reset in ::prepare)
    m_powerCost = m_spellInfo->CalcPowerCost(m_caster, m_spellSchoolMask);

    // check power requirement
    SpellCastResult failReason = CheckPower();
    if (failReason != SPELL_CAST_OK)
        return failReason;

    // Check if spell is affected by GCD
    if (m_spellInfo->StartRecoveryCategory > 0)
        if (m_caster->GetCharmInfo() && m_caster->GetSpellHistory()->HasGlobalCooldown(m_spellInfo))
            return SPELL_FAILED_NOT_READY;

    return CheckCast(true);
}

SpellCastResult Spell::CheckCasterAuras() const
{
    // spells totally immuned to caster auras (wsg flag drop, give marks etc)
    if (m_spellInfo->HasAttribute(SPELL_ATTR6_IGNORE_CASTER_AURAS))
        return SPELL_CAST_OK;

    uint8 school_immune = 0;
    uint32 mechanic_immune = 0;
    uint32 dispel_immune = 0;

    // Check if the spell grants school or mechanic immunity.
    // We use bitmasks so the loop is done only once and not on every aura check below.
    if (m_spellInfo->HasAttribute(SPELL_ATTR1_DISPEL_AURAS_ON_IMMUNITY))
    {
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (m_spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_SCHOOL_IMMUNITY)
                school_immune |= uint32(m_spellInfo->Effects[i].MiscValue);
            else if (m_spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_MECHANIC_IMMUNITY)
                mechanic_immune |= 1 << uint32(m_spellInfo->Effects[i].MiscValue);
            else if (m_spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_DISPEL_IMMUNITY)
                dispel_immune |= SpellInfo::GetDispelMask(DispelType(m_spellInfo->Effects[i].MiscValue));
        }
        // immune movement impairment and loss of control
        if (m_spellInfo->Id == 42292 || m_spellInfo->Id == 59752 || m_spellInfo->Id == 19574)
            mechanic_immune = IMMUNE_TO_MOVEMENT_IMPAIRMENT_AND_LOSS_CONTROL_MASK;
    }

    bool usableInStun = m_spellInfo->HasAttribute(SPELL_ATTR5_USABLE_WHILE_STUNNED);

    // Check whether the cast should be prevented by any state you might have.
    SpellCastResult prevented_reason = SPELL_CAST_OK;
    // Have to check if there is a stun aura. Otherwise will have problems with ghost aura apply while logging out
    uint32 unitflag = m_caster->GetUInt32Value(UNIT_FIELD_FLAGS);     // Get unit state
    if (unitflag & UNIT_FLAG_STUNNED)
    {
        // spell is usable while stunned, check if caster has only mechanic stun auras, another stun types must prevent cast spell
        if (usableInStun)
        {
            bool foundNotStun = false;
            Unit::AuraEffectList const& stunAuras = m_caster->GetAuraEffectsByType(SPELL_AURA_MOD_STUN);
            for (Unit::AuraEffectList::const_iterator i = stunAuras.begin(); i != stunAuras.end(); ++i)
            {
                if ((*i)->GetSpellInfo()->GetAllEffectsMechanicMask() && !((*i)->GetSpellInfo()->GetAllEffectsMechanicMask() & (1<<MECHANIC_STUN)))
                {
                    foundNotStun = true;
                    break;
                }
            }
            if (foundNotStun && m_spellInfo->Id != 22812)
                prevented_reason = SPELL_FAILED_STUNNED;
        }
        else
            prevented_reason = SPELL_FAILED_STUNNED;
    }
    else if (unitflag & UNIT_FLAG_CONFUSED && !m_spellInfo->HasAttribute(SPELL_ATTR5_USABLE_WHILE_CONFUSED))
        prevented_reason = SPELL_FAILED_CONFUSED;
    else if (unitflag & UNIT_FLAG_FLEEING && !m_spellInfo->HasAttribute(SPELL_ATTR5_USABLE_WHILE_FEARED))
        prevented_reason = SPELL_FAILED_FLEEING;
    else if (unitflag & UNIT_FLAG_SILENCED && m_spellInfo->PreventionType == SPELL_PREVENTION_TYPE_SILENCE)
        prevented_reason = SPELL_FAILED_SILENCED;
    else if (unitflag & UNIT_FLAG_PACIFIED && m_spellInfo->PreventionType == SPELL_PREVENTION_TYPE_PACIFY)
        prevented_reason = SPELL_FAILED_PACIFIED;

    // Attr must make flag drop spell totally immune from all effects
    if (prevented_reason != SPELL_CAST_OK)
    {
        if (school_immune || mechanic_immune || dispel_immune)
        {
            //Checking auras is needed now, because you are prevented by some state but the spell grants immunity.
            Unit::AuraApplicationMap const& auras = m_caster->GetAppliedAuras();
            for (Unit::AuraApplicationMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
            {
                Aura const* aura = itr->second->GetBase();
                SpellInfo const* auraInfo = aura->GetSpellInfo();
                if (auraInfo->GetAllEffectsMechanicMask() & mechanic_immune)
                    continue;
                if (auraInfo->GetSchoolMask() & school_immune && !auraInfo->HasAttribute(SPELL_ATTR1_UNAFFECTED_BY_SCHOOL_IMMUNE))
                    continue;
                if (auraInfo->GetDispelMask() & dispel_immune)
                    continue;

                //Make a second check for spell failed so the right SPELL_FAILED message is returned.
                //That is needed when your casting is prevented by multiple states and you are only immune to some of them.
                for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                {
                    if (AuraEffect* part = aura->GetEffect(i))
                    {
                        switch (part->GetAuraType())
                        {
                            case SPELL_AURA_MOD_STUN:
                                if (!usableInStun || !(auraInfo->GetAllEffectsMechanicMask() & (1 << MECHANIC_STUN)))
                                    return SPELL_FAILED_STUNNED;
                                break;
                            case SPELL_AURA_MOD_CONFUSE:
                                if (!m_spellInfo->HasAttribute(SPELL_ATTR5_USABLE_WHILE_CONFUSED))
                                    return SPELL_FAILED_CONFUSED;
                                break;
                            case SPELL_AURA_MOD_FEAR:
                                if (!m_spellInfo->HasAttribute(SPELL_ATTR5_USABLE_WHILE_FEARED))
                                    return SPELL_FAILED_FLEEING;
                                break;
                            case SPELL_AURA_MOD_SILENCE:
                            case SPELL_AURA_MOD_PACIFY:
                            case SPELL_AURA_MOD_PACIFY_SILENCE:
                                if (m_spellInfo->PreventionType == SPELL_PREVENTION_TYPE_PACIFY)
                                    return SPELL_FAILED_PACIFIED;
                                else if (m_spellInfo->PreventionType == SPELL_PREVENTION_TYPE_SILENCE)
                                    return SPELL_FAILED_SILENCED;
                                break;
                            default: break;
                        }
                    }
                }
            }
        }
        // You are prevented from casting and the spell cast does not grant immunity. Return a failed error.
        else
            return prevented_reason;
    }
    return SPELL_CAST_OK;
}

bool Spell::CanAutoCast(Unit* target)
{
    if (!target)
        return (CheckPetCast(target) == SPELL_CAST_OK);

    ObjectGuid targetguid = target->GetGUID();

    for (uint32 j = 0; j < MAX_SPELL_EFFECTS; ++j)
    {
        if (m_spellInfo->Effects[j].Effect == SPELL_EFFECT_APPLY_AURA)
        {
            if (m_spellInfo->StackAmount <= 1)
            {
                if (target->HasAuraEffect(m_spellInfo->Id, j))
                    return false;
            }
            else
            {
                if (AuraEffect* aureff = target->GetAuraEffect(m_spellInfo->Id, j))
                    if (aureff->GetBase()->GetStackAmount() >= m_spellInfo->StackAmount)
                        return false;
            }
        }
        else if (m_spellInfo->Effects[j].IsAreaAuraEffect())
        {
            if (target->HasAuraEffect(m_spellInfo->Id, j))
                return false;
        }
    }

    SpellCastResult result = CheckPetCast(target);
    if (result == SPELL_CAST_OK || result == SPELL_FAILED_UNIT_NOT_INFRONT)
    {
        // do not check targets for ground-targeted spells (we target them on top of the intended target anyway)
        if (GetSpellInfo()->ExplicitTargetMask & TARGET_FLAG_DEST_LOCATION)
            return true;
        SelectSpellTargets();
        //check if among target units, our WANTED target is as well (->only self cast spells return false)
        for (std::list<TargetInfo>::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
            if (ihit->targetGUID == targetguid)
                return true;
    }
    // either the cast failed or the intended target wouldn't be hit
    return false;
}

SpellCastResult Spell::CheckRange(bool strict)
{
    // Don't check for instant cast spells
    if (!strict && m_casttime == 0)
        return SPELL_CAST_OK;

    if (m_spellInfo->RangeEntry)
    {
        // self cast is used for triggered spells, no range checking needed
        if (m_spellInfo->RangeEntry->ID == 1)
            return SPELL_CAST_OK;
    }

    float minRange, maxRange;
    std::tie(minRange, maxRange) = GetMinMaxRange(strict);

    // get square values for sqr distance checks
    minRange *= minRange;
    maxRange *= maxRange;

    Unit* target = m_targets.GetUnitTarget();
    if (target && target != m_caster)
    {
        if (m_caster->GetExactDistSq(target) > maxRange)
            return SPELL_FAILED_OUT_OF_RANGE;

        if (minRange > 0.0f && m_caster->GetExactDistSq(target) < minRange)
            return SPELL_FAILED_OUT_OF_RANGE;

        if (m_caster->GetTypeId() == TYPEID_PLAYER &&
            (m_spellInfo->FacingCasterFlags & SPELL_FACING_FLAG_INFRONT) && !m_caster->HasInArc(static_cast<float>(M_PI), target))
            return SPELL_FAILED_UNIT_NOT_INFRONT;
    }

    if (m_targets.HasDst() && !m_targets.HasTraj())
    {
        if (m_caster->GetExactDistSq(m_targets.GetDstPos()) > maxRange)
            return !(_triggeredCastFlags & TRIGGERED_DONT_REPORT_CAST_ERROR) ? SPELL_FAILED_OUT_OF_RANGE : SPELL_FAILED_DONT_REPORT;
        if (minRange > 0.0f && m_caster->GetExactDistSq(m_targets.GetDstPos()) < minRange)
            return !(_triggeredCastFlags & TRIGGERED_DONT_REPORT_CAST_ERROR) ? SPELL_FAILED_OUT_OF_RANGE : SPELL_FAILED_DONT_REPORT;
    }

    return SPELL_CAST_OK;
}

std::pair<float, float> Spell::GetMinMaxRange(bool strict)
{
    float rangeMod = 0.0f;
    float minRange = 0.0f;
    float maxRange = 0.0f;
    if (strict && IsNextMeleeSwingSpell())
    {
        maxRange = 100.0f;
        return std::pair<float, float>(minRange, maxRange);
    }

    if (m_spellInfo->RangeEntry)
    {
        Unit* target = m_targets.GetUnitTarget();
        if (m_spellInfo->RangeEntry->type & SPELL_RANGE_MELEE)
        {
            rangeMod = m_caster->GetMeleeRange(target ? target : m_caster); // when the target is not a unit, take the caster's combat reach as the target's combat reach.
        }
        else
        {
            float meleeRange = 0.0f;
            if (m_spellInfo->RangeEntry->type & SPELL_RANGE_RANGED)
                meleeRange = m_caster->GetMeleeRange(target ? target : m_caster); // when the target is not a unit, take the caster's combat reach as the target's combat reach.

            minRange = m_caster->GetSpellMinRangeForTarget(target, m_spellInfo) + meleeRange;
            maxRange = m_caster->GetSpellMaxRangeForTarget(target, m_spellInfo);

            if (target || m_targets.GetCorpseTarget())
            {
                rangeMod = m_caster->GetCombatReach() + (target ? target->GetCombatReach() : m_caster->GetCombatReach());


                if (minRange > 0.0f && !(m_spellInfo->RangeEntry->type & SPELL_RANGE_RANGED))
                    minRange += rangeMod;
            }
        }

        if (target && m_caster->isMoving() && target->isMoving() && !m_caster->IsWalking() && !target->IsWalking() &&
            (m_spellInfo->RangeEntry->type & SPELL_RANGE_MELEE || target->GetTypeId() == TYPEID_PLAYER))
            rangeMod += 5.0f / 3.0f;
    }

    if (m_spellInfo->HasAttribute(SPELL_ATTR0_REQ_AMMO) && m_caster->GetTypeId() == TYPEID_PLAYER)
        if (Item* ranged = m_caster->ToPlayer()->GetWeaponForAttack(RANGED_ATTACK, true))
            maxRange *= ranged->GetTemplate()->RangedModRange * 0.01f;

	if (Player* modOwner = m_caster->GetSpellModOwner())
		modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_RANGE, maxRange, const_cast<Spell*>(this));

    if (!strict)
        maxRange += 5.f;

    maxRange += rangeMod;

    return std::pair<float, float>(minRange, maxRange);
}

SpellCastResult Spell::CheckPower()
{
    // item cast not used power
    if (m_CastItem)
        return SPELL_CAST_OK;

    // health as power used - need check health amount
    if (m_spellInfo->PowerType == POWER_HEALTH)
    {
        if (int32(m_caster->GetHealth()) <= m_powerCost)
            return SPELL_FAILED_CASTER_AURASTATE;
        return SPELL_CAST_OK;
    }
    // Check valid power type
    if (m_spellInfo->PowerType >= MAX_POWERS)
    {
        TC_LOG_ERROR("spells", "Spell::CheckPower: Unknown power type '%d'", m_spellInfo->PowerType);
        return SPELL_FAILED_UNKNOWN;
    }

    //check rune cost only if a spell has PowerType == POWER_RUNE
    if (m_spellInfo->PowerType == POWER_RUNE)
    {
        SpellCastResult failReason = CheckRuneCost(m_spellInfo->RuneCostID);
        if (failReason != SPELL_CAST_OK)
            return failReason;
    }

    // Check power amount
    Powers powerType = Powers(m_spellInfo->PowerType);
    if (int32(m_caster->GetPower(powerType)) < m_powerCost)
        return SPELL_FAILED_NO_POWER;
    else
        return SPELL_CAST_OK;
}

SpellCastResult Spell::CheckItems()
{
    Player* player = m_caster->ToPlayer();
    if (!player)
        return SPELL_CAST_OK;

    if (!m_CastItem)
    {
        if (m_castItemGUID)
            return SPELL_FAILED_ITEM_NOT_READY;
    }
    else
    {
        uint32 itemid = m_CastItem->GetEntry();
        if (!player->HasItemCount(itemid))
            return SPELL_FAILED_ITEM_NOT_READY;

        ItemTemplate const* proto = m_CastItem->GetTemplate();
        if (!proto)
            return SPELL_FAILED_ITEM_NOT_READY;

        for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
            if (proto->Spells[i].SpellCharges)
                if (m_CastItem->GetSpellCharges(i) == 0)
                    return SPELL_FAILED_NO_CHARGES_REMAIN;

        // consumable cast item checks
        if (proto->Class == ITEM_CLASS_CONSUMABLE && m_targets.GetUnitTarget())
        {
            // such items should only fail if there is no suitable effect at all - see Rejuvenation Potions for example
            SpellCastResult failReason = SPELL_CAST_OK;
            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                // skip check, pet not required like checks, and for TARGET_UNIT_PET m_targets.GetUnitTarget() is not the real target but the caster
                if (m_spellInfo->Effects[i].TargetA.GetTarget() == TARGET_UNIT_PET)
                    continue;

                if (m_spellInfo->Effects[i].Effect == SPELL_EFFECT_HEAL)
                {
                    if (m_targets.GetUnitTarget()->IsFullHealth())
                    {
                        failReason = SPELL_FAILED_ALREADY_AT_FULL_HEALTH;
                        continue;
                    }
                    else
                    {
                        failReason = SPELL_CAST_OK;
                        break;
                    }
                }

                // Mana Potion, Rage Potion, Thistle Tea(Rogue), ...
                if (m_spellInfo->Effects[i].Effect == SPELL_EFFECT_ENERGIZE)
                {
                    if (m_spellInfo->Effects[i].MiscValue < 0 || m_spellInfo->Effects[i].MiscValue >= int8(MAX_POWERS))
                    {
                        failReason = SPELL_FAILED_ALREADY_AT_FULL_POWER;
                        continue;
                    }

                    Powers power = Powers(m_spellInfo->Effects[i].MiscValue);
                    if (m_targets.GetUnitTarget()->GetPower(power) == m_targets.GetUnitTarget()->GetMaxPower(power))
                    {
                        failReason = SPELL_FAILED_ALREADY_AT_FULL_POWER;
                        continue;
                    }
                    else
                    {
                        failReason = SPELL_CAST_OK;
                        break;
                    }
                }
            }

            if (failReason != SPELL_CAST_OK)
                return failReason;
        }
    }

    // check target item
    if (m_targets.GetItemTargetGUID())
    {
        if (!m_targets.GetItemTarget())
            return SPELL_FAILED_ITEM_GONE;

        if (!m_targets.GetItemTarget()->IsFitToSpellRequirements(m_spellInfo))
            return SPELL_FAILED_EQUIPPED_ITEM_CLASS;
    }
    // if not item target then required item must be equipped
    else
    {
        if (!(_triggeredCastFlags & TRIGGERED_IGNORE_EQUIPPED_ITEM_REQUIREMENT))
            if (!player->HasItemFitToSpellRequirements(m_spellInfo))
                return SPELL_FAILED_EQUIPPED_ITEM_CLASS;
    }

    // do not take reagents for these item casts
    if (!(m_CastItem && m_CastItem->GetTemplate()->Flags & ITEM_PROTO_FLAG_TRIGGERED_CAST))
    {
        bool checkReagents = !(_triggeredCastFlags & TRIGGERED_IGNORE_POWER_AND_REAGENT_COST) && !player->CanNoReagentCast(m_spellInfo);
        // Not own traded item (in trader trade slot) requires reagents even if triggered spell
        if (!checkReagents)
            if (Item* targetItem = m_targets.GetItemTarget())
                if (targetItem->GetOwnerGUID() != m_caster->GetGUID())
                    checkReagents = true;

        // check reagents (ignore triggered spells with reagents processed by original spell) and special reagent ignore case.
        if (checkReagents)
        {
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; i++)
            {
                if (m_spellInfo->Reagent[i] <= 0)
                    continue;

                uint32 itemid    = m_spellInfo->Reagent[i];
                uint32 itemcount = m_spellInfo->ReagentCount[i];

                // if CastItem is also spell reagent
                if (m_CastItem && m_CastItem->GetEntry() == itemid)
                {
                    ItemTemplate const* proto = m_CastItem->GetTemplate();
                    if (!proto)
                        return SPELL_FAILED_ITEM_NOT_READY;
                    for (uint8 s = 0; s < MAX_ITEM_PROTO_SPELLS; ++s)
                    {
                        // CastItem will be used up and does not count as reagent
                        int32 charges = m_CastItem->GetSpellCharges(s);
                        if (proto->Spells[s].SpellCharges < 0 && abs(charges) < 2)
                        {
                            ++itemcount;
                            break;
                        }
                    }
                }
                if (!player->HasItemCount(itemid, itemcount))
                    return SPELL_FAILED_REAGENTS;
            }
        }

        // check totem-item requirements (items presence in inventory)
        uint32 totems = 2;
        for (uint8 i = 0; i < 2; ++i)
        {
            if (m_spellInfo->Totem[i] != 0)
            {
                if (player->HasItemCount(m_spellInfo->Totem[i]))
                {
                    totems -= 1;
                    continue;
                }
            }
            else
                totems -= 1;
        }
        if (totems != 0)
            return SPELL_FAILED_TOTEMS;                         //0x7C

        // Check items for TotemCategory  (items presence in inventory)
        uint32 TotemCategory = 2;
        for (uint8 i = 0; i < 2; ++i)
        {
            if (m_spellInfo->TotemCategory[i] != 0)
            {
                if (player->HasItemTotemCategory(m_spellInfo->TotemCategory[i]))
                {
                    TotemCategory -= 1;
                    continue;
                }
            }
            else
                TotemCategory -= 1;
        }
        if (TotemCategory != 0)
            return SPELL_FAILED_TOTEM_CATEGORY;                 //0x7B
    }

    // special checks for spell effects
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        switch (m_spellInfo->Effects[i].Effect)
        {
            case SPELL_EFFECT_CREATE_ITEM:
            case SPELL_EFFECT_CREATE_ITEM_2:
            {
                if (!IsTriggered() && m_spellInfo->Effects[i].ItemType)
                {
                    ItemPosCountVec dest;
                    InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, m_spellInfo->Effects[i].ItemType, 1);
                    if (msg != EQUIP_ERR_OK)
                    {
                        ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(m_spellInfo->Effects[i].ItemType);
                        /// @todo Needs review
                        if (pProto && !(pProto->ItemLimitCategory))
                        {
                            player->SendEquipError(msg, NULL, NULL, m_spellInfo->Effects[i].ItemType);
                            return SPELL_FAILED_DONT_REPORT;
                        }
                        else
                        {
                            if (!(m_spellInfo->SpellFamilyName == SPELLFAMILY_MAGE && (m_spellInfo->SpellFamilyFlags[0] & 0x40000000)))
                                return SPELL_FAILED_TOO_MANY_OF_ITEM;
                            else if (!(player->HasItemCount(m_spellInfo->Effects[i].ItemType)))
                                return SPELL_FAILED_TOO_MANY_OF_ITEM;
                            else
                                player->CastSpell(m_caster, m_spellInfo->Effects[EFFECT_1].CalcValue(), false);        // move this to anywhere
                            return SPELL_FAILED_DONT_REPORT;
                        }
                    }
                }
                break;
            }
            case SPELL_EFFECT_ENCHANT_ITEM:
                if (m_spellInfo->Effects[i].ItemType && m_targets.GetItemTarget()
                    && (m_targets.GetItemTarget()->IsWeaponVellum() || m_targets.GetItemTarget()->IsArmorVellum()))
                {
                    // cannot enchant vellum for other player
                    if (m_targets.GetItemTarget()->GetOwner() != m_caster)
                        return SPELL_FAILED_NOT_TRADEABLE;
                    // do not allow to enchant vellum from scroll made by vellum-prevent exploit
                    if (m_CastItem && m_CastItem->GetTemplate()->Flags & ITEM_PROTO_FLAG_TRIGGERED_CAST)
                        return SPELL_FAILED_TOTEM_CATEGORY;
                    ItemPosCountVec dest;
                    InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, m_spellInfo->Effects[i].ItemType, 1);
                    if (msg != EQUIP_ERR_OK)
                    {
                        player->SendEquipError(msg, NULL, NULL, m_spellInfo->Effects[i].ItemType);
                        return SPELL_FAILED_DONT_REPORT;
                    }
                }
                // no break
            case SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC:
            {
                Item* targetItem = m_targets.GetItemTarget();
                if (!targetItem)
                    return SPELL_FAILED_ITEM_NOT_FOUND;

                if (targetItem->GetTemplate()->ItemLevel < m_spellInfo->BaseLevel)
                    return SPELL_FAILED_LOWLEVEL;

                bool isItemUsable = false;
                for (uint8 e = 0; e < MAX_ITEM_PROTO_SPELLS; ++e)
                {
                    ItemTemplate const* proto = targetItem->GetTemplate();
                    if (proto->Spells[e].SpellId && (
                        proto->Spells[e].SpellTrigger == ITEM_SPELLTRIGGER_ON_USE ||
                        proto->Spells[e].SpellTrigger == ITEM_SPELLTRIGGER_ON_NO_DELAY_USE))
                    {
                        isItemUsable = true;
                        break;
                    }
                }

                SpellItemEnchantmentEntry const* enchantEntry = sSpellItemEnchantmentStore.LookupEntry(m_spellInfo->Effects[i].MiscValue);
                // do not allow adding usable enchantments to items that have use effect already
                if (enchantEntry)
                {
                    for (uint8 s = 0; s < MAX_ITEM_ENCHANTMENT_EFFECTS; ++s)
                    {
                        switch (enchantEntry->type[s])
                        {
                            case ITEM_ENCHANTMENT_TYPE_USE_SPELL:
                                if (isItemUsable)
                                    return SPELL_FAILED_ON_USE_ENCHANT;
                                break;
                            case ITEM_ENCHANTMENT_TYPE_PRISMATIC_SOCKET:
                            {
                                uint32 numSockets = 0;
                                for (uint32 socket = 0; socket < MAX_ITEM_PROTO_SOCKETS; ++socket)
                                    if (targetItem->GetTemplate()->Socket[socket].Color)
                                        ++numSockets;

                                if (numSockets == MAX_ITEM_PROTO_SOCKETS || targetItem->GetEnchantmentId(PRISMATIC_ENCHANTMENT_SLOT))
                                    return SPELL_FAILED_MAX_SOCKETS;
                                break;
                            }
                        }
                    }
                }

                // Not allow enchant in trade slot for some enchant type
                if (targetItem->GetOwner() != m_caster)
                {
                    if (!enchantEntry)
                        return SPELL_FAILED_ERROR;
                    if (enchantEntry->slot & ENCHANTMENT_CAN_SOULBOUND)
                        return SPELL_FAILED_NOT_TRADEABLE;
                }
                break;
            }
            case SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY:
            {
                Item* item = m_targets.GetItemTarget();
                if (!item)
                    return SPELL_FAILED_ITEM_NOT_FOUND;
                // Not allow enchant in trade slot for some enchant type
                if (item->GetOwner() != m_caster)
                {
                    uint32 enchant_id = m_spellInfo->Effects[i].MiscValue;
                    SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
                    if (!pEnchant)
                        return SPELL_FAILED_ERROR;
                    if (pEnchant->slot & ENCHANTMENT_CAN_SOULBOUND)
                        return SPELL_FAILED_NOT_TRADEABLE;
                }
                break;
            }
            case SPELL_EFFECT_ENCHANT_HELD_ITEM:
                // check item existence in effect code (not output errors at offhand hold item effect to main hand for example
                break;
            case SPELL_EFFECT_DISENCHANT:
            {
                if (!m_targets.GetItemTarget())
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;

                // prevent disenchanting in trade slot
                if (m_targets.GetItemTarget()->GetOwnerGUID() != m_caster->GetGUID())
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;

                ItemTemplate const* itemProto = m_targets.GetItemTarget()->GetTemplate();
                if (!itemProto)
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;

                uint32 item_quality = itemProto->Quality;
                // 2.0.x addon: Check player enchanting level against the item disenchanting requirements
                uint32 item_disenchantskilllevel = itemProto->RequiredDisenchantSkill;
                if (item_disenchantskilllevel == uint32(-1))
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;
                if (item_disenchantskilllevel > player->GetSkillValue(SKILL_ENCHANTING))
                    return SPELL_FAILED_LOW_CASTLEVEL;
                if (item_quality > 4 || item_quality < 2)
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;
                if (itemProto->Class != ITEM_CLASS_WEAPON && itemProto->Class != ITEM_CLASS_ARMOR)
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;
                if (!itemProto->DisenchantID)
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;
                break;
            }
            case SPELL_EFFECT_PROSPECTING:
            {
                if (!m_targets.GetItemTarget())
                    return SPELL_FAILED_CANT_BE_PROSPECTED;
                //ensure item is a prospectable ore
                if (!(m_targets.GetItemTarget()->GetTemplate()->Flags & ITEM_PROTO_FLAG_PROSPECTABLE))
                    return SPELL_FAILED_CANT_BE_PROSPECTED;
                //prevent prospecting in trade slot
                if (m_targets.GetItemTarget()->GetOwnerGUID() != m_caster->GetGUID())
                    return SPELL_FAILED_CANT_BE_PROSPECTED;
                //Check for enough skill in jewelcrafting
                uint32 item_prospectingskilllevel = m_targets.GetItemTarget()->GetTemplate()->RequiredSkillRank;
                if (item_prospectingskilllevel >player->GetSkillValue(SKILL_JEWELCRAFTING))
                    return SPELL_FAILED_LOW_CASTLEVEL;
                //make sure the player has the required ores in inventory
                if (m_targets.GetItemTarget()->GetCount() < 5)
                    return SPELL_FAILED_NEED_MORE_ITEMS;

                if (!LootTemplates_Prospecting.HaveLootFor(m_targets.GetItemTargetEntry()))
                    return SPELL_FAILED_CANT_BE_PROSPECTED;

                break;
            }
            case SPELL_EFFECT_MILLING:
            {
                if (!m_targets.GetItemTarget())
                    return SPELL_FAILED_CANT_BE_MILLED;
                //ensure item is a millable herb
                if (!(m_targets.GetItemTarget()->GetTemplate()->Flags & ITEM_PROTO_FLAG_MILLABLE))
                    return SPELL_FAILED_CANT_BE_MILLED;
                //prevent milling in trade slot
                if (m_targets.GetItemTarget()->GetOwnerGUID() != m_caster->GetGUID())
                    return SPELL_FAILED_CANT_BE_MILLED;
                //Check for enough skill in inscription
                uint32 item_millingskilllevel = m_targets.GetItemTarget()->GetTemplate()->RequiredSkillRank;
                if (item_millingskilllevel > player->GetSkillValue(SKILL_INSCRIPTION))
                    return SPELL_FAILED_LOW_CASTLEVEL;
                //make sure the player has the required herbs in inventory
                if (m_targets.GetItemTarget()->GetCount() < 5)
                    return SPELL_FAILED_NEED_MORE_ITEMS;

                if (!LootTemplates_Milling.HaveLootFor(m_targets.GetItemTargetEntry()))
                    return SPELL_FAILED_CANT_BE_MILLED;

                break;
            }
            case SPELL_EFFECT_WEAPON_DAMAGE:
            case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
            {
                if (m_attackType != RANGED_ATTACK)
                    break;

                Item* pItem = player->GetWeaponForAttack(m_attackType);
                if (!pItem || pItem->IsBroken())
                    return SPELL_FAILED_EQUIPPED_ITEM;

                switch (pItem->GetTemplate()->SubClass)
                {
                    case ITEM_SUBCLASS_WEAPON_THROWN:
                    {
                        uint32 ammo = pItem->GetEntry();
                        if (!player->HasItemCount(ammo))
                            return SPELL_FAILED_NO_AMMO;
                        break;
                    }
                    case ITEM_SUBCLASS_WEAPON_GUN:
                    case ITEM_SUBCLASS_WEAPON_BOW:
                    case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                    {
                        uint32 ammo = player->GetUInt32Value(PLAYER_AMMO_ID);
                        if (!ammo)
                        {
                            // Requires No Ammo
                            if (m_caster->HasAura(46699))
                                break;                      // skip other checks

                            return SPELL_FAILED_NO_AMMO;
                        }

                        ItemTemplate const* ammoProto = sObjectMgr->GetItemTemplate(ammo);
                        if (!ammoProto)
                            return SPELL_FAILED_NO_AMMO;

                        if (ammoProto->Class != ITEM_CLASS_PROJECTILE)
                            return SPELL_FAILED_NO_AMMO;

                        // check ammo ws. weapon compatibility
                        switch (pItem->GetTemplate()->SubClass)
                        {
                            case ITEM_SUBCLASS_WEAPON_BOW:
                            case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                                if (ammoProto->SubClass != ITEM_SUBCLASS_ARROW)
                                    return SPELL_FAILED_NO_AMMO;
                                break;
                            case ITEM_SUBCLASS_WEAPON_GUN:
                                if (ammoProto->SubClass != ITEM_SUBCLASS_BULLET)
                                    return SPELL_FAILED_NO_AMMO;
                                break;
                            default:
                                return SPELL_FAILED_NO_AMMO;
                        }

                        if (!player->HasItemCount(ammo))
                        {
                            player->SetUInt32Value(PLAYER_AMMO_ID, 0);
                            return SPELL_FAILED_NO_AMMO;
                        }
                        break;
                    }
                    case ITEM_SUBCLASS_WEAPON_WAND:
                        break;
                    default:
                        break;
                }
                break;
            }
            case SPELL_EFFECT_CREATE_MANA_GEM:
            {
                 uint32 item_id = m_spellInfo->Effects[i].ItemType;
                 ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(item_id);

                 if (!pProto)
                     return SPELL_FAILED_ITEM_AT_MAX_CHARGES;

                 if (Item* pitem = player->GetItemByEntry(item_id))
                 {
                     for (int x = 0; x < MAX_ITEM_PROTO_SPELLS; ++x)
                         if (pProto->Spells[x].SpellCharges != 0 && pitem->GetSpellCharges(x) == pProto->Spells[x].SpellCharges)
                             return SPELL_FAILED_ITEM_AT_MAX_CHARGES;
                 }
                 break;
            }
            default:
                break;
        }
    }

    // check weapon presence in slots for main/offhand weapons
    if (!(_triggeredCastFlags & TRIGGERED_IGNORE_EQUIPPED_ITEM_REQUIREMENT) && m_spellInfo->EquippedItemClass >=0)
    {
        // main hand weapon required
        if (m_spellInfo->HasAttribute(SPELL_ATTR3_MAIN_HAND))
        {
            Item* item = m_caster->ToPlayer()->GetWeaponForAttack(BASE_ATTACK);

            // skip spell if no weapon in slot or broken
            if (!item || item->IsBroken())
                return (_triggeredCastFlags & TRIGGERED_DONT_REPORT_CAST_ERROR) ? SPELL_FAILED_DONT_REPORT : SPELL_FAILED_EQUIPPED_ITEM_CLASS;

            // skip spell if weapon not fit to triggered spell
            if (!item->IsFitToSpellRequirements(m_spellInfo))
                return (_triggeredCastFlags & TRIGGERED_DONT_REPORT_CAST_ERROR) ? SPELL_FAILED_DONT_REPORT : SPELL_FAILED_EQUIPPED_ITEM_CLASS;
        }

        // offhand hand weapon required
        if (m_spellInfo->HasAttribute(SPELL_ATTR3_REQ_OFFHAND))
        {
            Item* item = m_caster->ToPlayer()->GetWeaponForAttack(OFF_ATTACK);

            // skip spell if no weapon in slot or broken
            if (!item || item->IsBroken())
                return (_triggeredCastFlags & TRIGGERED_DONT_REPORT_CAST_ERROR) ? SPELL_FAILED_DONT_REPORT : SPELL_FAILED_EQUIPPED_ITEM_CLASS;

            // skip spell if weapon not fit to triggered spell
            if (!item->IsFitToSpellRequirements(m_spellInfo))
                return (_triggeredCastFlags & TRIGGERED_DONT_REPORT_CAST_ERROR) ? SPELL_FAILED_DONT_REPORT : SPELL_FAILED_EQUIPPED_ITEM_CLASS;
        }
    }

    return SPELL_CAST_OK;
}

void Spell::Delayed() // only called in DealDamage()
{
    if (!m_caster)// || m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    //if (m_spellState == SPELL_STATE_DELAYED)
    //    return;                                             // spell is active and can't be time-backed

    if (isDelayableNoMore())                                 // Spells may only be delayed twice
        return;

    // spells not loosing casting time (slam, dynamites, bombs..)
    //if (!(m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_DAMAGE))
    //    return;

    //check pushback reduce
    int32 delaytime = 500;                                  // spellcasting delay is normally 500ms
    int32 delayReduce = 100;                                // must be initialized to 100 for percent modifiers
    m_caster->ToPlayer()->ApplySpellMod(m_spellInfo->Id, SPELLMOD_NOT_LOSE_CASTING_TIME, delayReduce, this);
    delayReduce += m_caster->GetTotalAuraModifier(SPELL_AURA_REDUCE_PUSHBACK) - 100;
    if (delayReduce >= 100)
        return;

    AddPct(delaytime, -delayReduce);

    if (m_timer + delaytime > m_casttime)
    {
        delaytime = m_casttime - m_timer;
        m_timer = m_casttime;
    }
    else
        m_timer += delaytime;

    TC_LOG_DEBUG("spells", "Spell %u partially interrupted for (%d) ms at damage", m_spellInfo->Id, delaytime);

    WorldPacket data(SMSG_SPELL_DELAYED, 8+4);
    data << m_caster->GetPackGUID();
    data << uint32(delaytime);

    m_caster->SendMessageToSet(data, true);
}

void Spell::DelayedChannel()
{
    if (!m_caster || m_caster->GetTypeId() != TYPEID_PLAYER || getState() != SPELL_STATE_CASTING)
        return;

    if (isDelayableNoMore())                                    // Spells may only be delayed twice
        return;

    //check pushback reduce
    // should be affected by modifiers, not take the dbc duration.
    int32 duration = ((m_channeledDuration > 0) ? m_channeledDuration : m_spellInfo->GetDuration());

    int32 delaytime = CalculatePct(duration, 25); // channeling delay is normally 25% of its time per hit
    int32 delayReduce = 100;                                    // must be initialized to 100 for percent modifiers
    m_caster->ToPlayer()->ApplySpellMod(m_spellInfo->Id, SPELLMOD_NOT_LOSE_CASTING_TIME, delayReduce, this);
    delayReduce += m_caster->GetTotalAuraModifier(SPELL_AURA_REDUCE_PUSHBACK) - 100;
    if (delayReduce >= 100)
        return;

    AddPct(delaytime, -delayReduce);

    if (m_timer <= delaytime)
    {
        delaytime = m_timer;
        m_timer = 0;
    }
    else
        m_timer -= delaytime;

    TC_LOG_DEBUG("spells", "Spell %u partially interrupted for %i ms, new duration: %u ms", m_spellInfo->Id, delaytime, m_timer);

    for (std::list<TargetInfo>::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
        if ((*ihit).missCondition == SPELL_MISS_NONE)
            if (Unit* unit = (m_caster->GetGUID() == ihit->targetGUID) ? m_caster : ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID))
                unit->DelayOwnedAuras(m_spellInfo->Id, m_originalCasterGUID, delaytime);

    // partially interrupt persistent area auras
    if (DynamicObject* dynObj = m_caster->GetDynObject(m_spellInfo->Id))
        dynObj->Delay(delaytime);

    SendChannelUpdate(m_timer);
}

bool Spell::UpdatePointers()
{
    if (m_originalCasterGUID == m_caster->GetGUID())
        m_originalCaster = m_caster;
    else
    {
        m_originalCaster = ObjectAccessor::GetUnit(*m_caster, m_originalCasterGUID);
        if (m_originalCaster && !m_originalCaster->IsInWorld())
            m_originalCaster = NULL;
    }

    if (m_castItemGUID && m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        m_CastItem = m_caster->ToPlayer()->GetItemByGuid(m_castItemGUID);
        // cast item not found, somehow the item is no longer where we expected
        if (!m_CastItem)
            return false;

        // check if the item is really the same, in case it has been wrapped for example
        if (m_castItemEntry != m_CastItem->GetEntry())
            return false;
    }

    m_targets.Update(m_caster);

    // further actions done only for dest targets
    if (!m_targets.HasDst())
        return true;

    // cache last transport
    WorldObject* transport = NULL;

    // update effect destinations (in case of moved transport dest target)
    for (uint8 effIndex = 0; effIndex < MAX_SPELL_EFFECTS; ++effIndex)
    {
        SpellDestination& dest = m_destTargets[effIndex];
        if (!dest._transportGUID)
            continue;

        if (!transport || transport->GetGUID() != dest._transportGUID)
            transport = ObjectAccessor::GetWorldObject(*m_caster, dest._transportGUID);

        if (transport)
        {
            dest._position.Relocate(transport);
            dest._position.RelocateOffset(dest._transportOffset);
        }
    }

    return true;
}

CurrentSpellTypes Spell::GetCurrentContainer() const
{
    if (IsNextMeleeSwingSpell())
        return(CURRENT_MELEE_SPELL);
    else if (IsAutoRepeat())
        return(CURRENT_AUTOREPEAT_SPELL);
    else if (m_spellInfo->IsChanneled())
        return(CURRENT_CHANNELED_SPELL);
    else
        return(CURRENT_GENERIC_SPELL);
}

bool Spell::CheckEffectTarget(Unit const* target, uint32 eff) const
{
    switch (m_spellInfo->Effects[eff].ApplyAuraName)
    {
        case SPELL_AURA_MOD_POSSESS:
        case SPELL_AURA_MOD_CHARM:
        case SPELL_AURA_MOD_POSSESS_PET:
        case SPELL_AURA_AOE_CHARM:
            if (target->GetTypeId() == TYPEID_UNIT && target->IsVehicle())
                return false;
            if (target->IsMounted())
                return false;
            if (target->GetCharmerGUID())
                return false;
            if (int32 damage = CalculateDamage(eff, target))
                if ((int32)target->getLevel() > damage)
                    return false;
            break;
        default:
            break;
    }

    if ((IsTriggered() && m_caster->GetMap()->GetId() == 631) || m_spellInfo->HasAttribute(SPELL_ATTR2_CAN_TARGET_NOT_IN_LOS) || DisableMgr::IsDisabledFor(DISABLE_TYPE_SPELL, m_spellInfo->Id, NULL, SPELL_DISABLE_LOS))
        return true;

    // Spells like Shadowfury
    // Check from center of destination if spell can hit target
    if (m_spellInfo->Targets & TARGET_FLAG_DEST_LOCATION || m_spellInfo->Effects[eff].TargetA.GetTarget() == TARGET_DEST_CHANNEL_TARGET)
        if (m_targets.HasDst() && !m_targets.HasTraj())
        {
            WorldLocation const* pos = m_targets.GetDstPos();
            return target->IsWithinLOS(pos->GetPositionX(), pos->GetPositionY(), pos->GetPositionZ());
        }

    if (IsTriggered() && !m_spellInfo->TriggeredEffectNeedsLosChecks(eff))
        return true;

    /// @todo: shit below shouldn't be here, but it's temporary
    //Check targets for LOS visibility (except spells without range limitations)
    bool effectIsTargetingArea = m_spellInfo->EffectIsTargetingArea(eff);
    switch (m_spellInfo->Effects[eff].Effect)
    {
        case SPELL_EFFECT_RESURRECT_NEW:
            // player far away, maybe his corpse near?
            if (target != m_caster && !target->IsWithinLOSInMap(m_caster))
            {
                if (!m_targets.GetCorpseTargetGUID())
                    return false;

                Corpse* corpse = ObjectAccessor::GetCorpse(*m_caster, m_targets.GetCorpseTargetGUID());
                if (!corpse)
                    return false;

                if (target->GetGUID() != corpse->GetOwnerGUID())
                    return false;

                if (!corpse->IsWithinLOSInMap(m_caster))
                    return false;
            }

            // all ok by some way or another, skip normal check
            break;
        default:                                            // normal case
            // Get GO cast coordinates if original caster -> GO
            WorldObject* caster = NULL;
            if (m_originalCasterGUID.IsGameObject())
                caster = m_caster->GetMap()->GetGameObject(m_originalCasterGUID);
            if (!caster)
                caster = m_caster;
            if (target != m_caster && effectIsTargetingArea && !target->IsWithinLOSInMap(caster))
                return false;
            break;
    }

    return true;
}

bool Spell::IsNextMeleeSwingSpell() const
{
    return m_spellInfo->HasAttribute(SPELL_ATTR0_ON_NEXT_SWING);
}

bool Spell::IsAutoActionResetSpell() const
{
    /// @todo changed SPELL_INTERRUPT_FLAG_AUTOATTACK -> SPELL_INTERRUPT_FLAG_INTERRUPT to fix compile - is this check correct at all?
    return !IsTriggered() && (m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT);
}

bool Spell::IsNeedSendToClient() const
{
    return m_spellInfo->SpellVisual[0] || m_spellInfo->SpellVisual[1] || m_spellInfo->IsChanneled() ||
        m_spellInfo->Speed > 0.0f || (!m_triggeredByAuraSpell && !IsTriggered());
}

bool Spell::HaveTargetsForEffect(uint8 effect) const
{
    for (std::list<TargetInfo>::const_iterator itr = m_UniqueTargetInfo.begin(); itr != m_UniqueTargetInfo.end(); ++itr)
        if (itr->effectMask & (1 << effect))
            return true;

    for (std::list<GOTargetInfo>::const_iterator itr = m_UniqueGOTargetInfo.begin(); itr != m_UniqueGOTargetInfo.end(); ++itr)
        if (itr->effectMask & (1 << effect))
            return true;

    for (std::list<ItemTargetInfo>::const_iterator itr = m_UniqueItemInfo.begin(); itr != m_UniqueItemInfo.end(); ++itr)
        if (itr->effectMask & (1 << effect))
            return true;

    return false;
}

SpellEvent::SpellEvent(Spell* spell) : BasicEvent()
{
    m_Spell = spell;
}

SpellEvent::~SpellEvent()
{
    if (m_Spell->getState() != SPELL_STATE_FINISHED)
        m_Spell->cancel();

    if (m_Spell->IsDeletable())
    {
        delete m_Spell;
    }
    else
    {
        TC_LOG_ERROR("spells", "~SpellEvent: %s %u tried to delete non-deletable spell %u. Was not deleted, causes memory leak.",
            (m_Spell->GetCaster()->GetTypeId() == TYPEID_PLAYER ? "Player" : "Creature"), m_Spell->GetCaster()->GetGUID().GetCounter(), m_Spell->m_spellInfo->Id);
        ABORT();
    }
}

bool SpellEvent::Execute(uint64 e_time, uint32 p_time)
{
    // update spell if it is not finished
    if (m_Spell->getState() != SPELL_STATE_FINISHED)
        m_Spell->update(p_time);

    // check spell state to process
    switch (m_Spell->getState())
    {
        case SPELL_STATE_FINISHED:
        {
            // spell was finished, check deletable state
            if (m_Spell->IsDeletable())
            {
                // check, if we do have unfinished triggered spells
                return true;                                // spell is deletable, finish event
            }
            // event will be re-added automatically at the end of routine)
        } break;

        case SPELL_STATE_DELAYED:
        {
            // first, check, if we have just started
            if (m_Spell->GetDelayStart() != 0)
            {
                // no, we aren't, do the typical update
                // check, if we have channeled spell on our hands
                /*
                if (m_Spell->m_spellInfo->IsChanneled())
                {
                    // evented channeled spell is processed separately, cast once after delay, and not destroyed till finish
                    // check, if we have casting anything else except this channeled spell and autorepeat
                    if (m_Spell->GetCaster()->IsNonMeleeSpellCast(false, true, true))
                    {
                        // another non-melee non-delayed spell is cast now, abort
                        m_Spell->cancel();
                    }
                    else
                    {
                        // Set last not triggered spell for apply spellmods
                        ((Player*)m_Spell->GetCaster())->SetSpellModTakingSpell(m_Spell, true);
                        // do the action (pass spell to channeling state)
                        m_Spell->handle_immediate();

                        // And remove after effect handling
                        ((Player*)m_Spell->GetCaster())->SetSpellModTakingSpell(m_Spell, false);
                    }
                    // event will be re-added automatically at the end of routine)
                }
                else
                */
                {
                    // run the spell handler and think about what we can do next
                    uint64 t_offset = e_time - m_Spell->GetDelayStart();
                    uint64 n_offset = m_Spell->handle_delayed(t_offset);
                    if (n_offset)
                    {
                        // re-add us to the queue
                        m_Spell->GetCaster()->m_Events.AddEvent(this, m_Spell->GetDelayStart() + n_offset, false);
                        return false;                       // event not complete
                    }
                    // event complete
                    // finish update event will be re-added automatically at the end of routine)
                }
            }
            else
            {
                // delaying had just started, record the moment
                m_Spell->SetDelayStart(e_time);
                // re-plan the event for the delay moment
                m_Spell->GetCaster()->m_Events.AddEvent(this, e_time + m_Spell->GetDelayMoment(), false);
                return false;                               // event not complete
            }
        } break;

        default:
        {
            // all other states
            // event will be re-added automatically at the end of routine)
        } break;
    }

    // spell processing not complete, plan event on the next update interval
    m_Spell->GetCaster()->m_Events.AddEvent(this, e_time + 1, false);
    return false;                                           // event not complete
}

void SpellEvent::Abort(uint64 /*e_time*/)
{
    // oops, the spell we try to do is aborted
    if (m_Spell->getState() != SPELL_STATE_FINISHED)
        m_Spell->cancel();
}

bool SpellEvent::IsDeletable() const
{
    return m_Spell->IsDeletable();
}

bool Spell::IsValidDeadOrAliveTarget(Unit const* target) const
{
    if (target->IsAlive())
        return !m_spellInfo->IsRequiringDeadTarget();
    if (m_spellInfo->IsAllowingDeadTarget())
        return true;
    return false;
}

void Spell::HandleLaunchPhase()
{
    // handle effects with SPELL_EFFECT_HANDLE_LAUNCH mode
    for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        // don't do anything for empty effect
        if (!m_spellInfo->Effects[i].IsEffect())
            continue;

        HandleEffects(NULL, NULL, NULL, i, SPELL_EFFECT_HANDLE_LAUNCH);
    }

    float multiplier[MAX_SPELL_EFFECTS];
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        if (m_applyMultiplierMask & (1 << i))
            multiplier[i] = m_spellInfo->Effects[i].CalcDamageMultiplier(m_originalCaster, this);

    bool usesAmmo = m_spellInfo->HasAttribute(SPELL_ATTR0_REQ_AMMO);

    Unit::AuraEffectList const& Auras = m_caster->GetAuraEffectsByType(SPELL_AURA_ABILITY_CONSUME_NO_AMMO);
    for (Unit::AuraEffectList::const_iterator j = Auras.begin(); j != Auras.end(); ++j)
    {
        if ((*j)->IsAffectedOnSpell(m_spellInfo))
            usesAmmo = false;
    }

    for (std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        TargetInfo& target = *ihit;

        uint32 mask = target.effectMask;
        if (!mask)
            continue;

        DoAllEffectOnLaunchTarget(target, multiplier);
    }

    if (usesAmmo)
        TakeAmmo();
}

void Spell::DoAllEffectOnLaunchTarget(TargetInfo& targetInfo, float* multiplier)
{
    Unit* unit = NULL;
    // In case spell hit target, do all effect on that target
    if (targetInfo.missCondition == SPELL_MISS_NONE)
        unit = m_caster->GetGUID() == targetInfo.targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, targetInfo.targetGUID);
    // In case spell reflect from target, do all effect on caster (if hit)
    else if (targetInfo.missCondition == SPELL_MISS_REFLECT && targetInfo.reflectResult == SPELL_MISS_NONE)
        unit = m_caster;
    if (!unit)
        return;

    for (uint32 i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        if (targetInfo.effectMask & (1<<i))
        {
            m_damage = 0;
            m_healing = 0;

            HandleEffects(unit, NULL, NULL, i, SPELL_EFFECT_HANDLE_LAUNCH_TARGET);

            if (m_damage > 0)
            {
                if (m_spellInfo->Effects[i].IsTargetingArea() || m_spellInfo->Effects[i].IsAreaAuraEffect() ||
                    m_spellInfo->Effects[i].IsEffect(SPELL_EFFECT_PERSISTENT_AREA_AURA) || m_spellInfo->AttributesEx5 & SPELL_ATTR5_REDUCED_BY_AOE_DAMAGE_AVOIDANCE)
                {
                    m_damage = int32(float(m_damage) * unit->GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_AOE_DAMAGE_AVOIDANCE, m_spellInfo->SchoolMask));
                    if (m_caster->GetTypeId() != TYPEID_PLAYER)
                        m_damage = int32(float(m_damage) * unit->GetTotalAuraMultiplierByMiscMask(SPELL_AURA_MOD_CREATURE_AOE_DAMAGE_AVOIDANCE, m_spellInfo->SchoolMask));
                }
            }

            if (m_applyMultiplierMask & (1 << i))
            {
                m_damage = int32(m_damage * m_damageMultipliers[i]);
                m_damageMultipliers[i] *= multiplier[i];
            }
            targetInfo.damage += m_damage;
        }
    }

    targetInfo.crit = m_caster->isSpellCrit(unit, m_spellInfo, m_spellSchoolMask, m_attackType);
}

SpellCastResult Spell::CanOpenLock(uint32 effIndex, uint32 lockId, SkillType& skillId, int32& reqSkillValue, int32& skillValue)
{
    if (!lockId)                                             // possible case for GO and maybe for items.
        return SPELL_CAST_OK;

    // Get LockInfo
    LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);

    if (!lockInfo)
        return SPELL_FAILED_BAD_TARGETS;

    bool reqKey = false;                                    // some locks not have reqs

    for (int j = 0; j < MAX_LOCK_CASE; ++j)
    {
        switch (lockInfo->Type[j])
        {
            // check key item (many fit cases can be)
            case LOCK_KEY_ITEM:
                if (lockInfo->Index[j] && m_CastItem && m_CastItem->GetEntry() == lockInfo->Index[j])
                    return SPELL_CAST_OK;
                reqKey = true;
                break;
                // check key skill (only single first fit case can be)
            case LOCK_KEY_SKILL:
            {
                reqKey = true;

                // wrong locktype, skip
                if (uint32(m_spellInfo->Effects[effIndex].MiscValue) != lockInfo->Index[j])
                    continue;

                skillId = SkillByLockType(LockType(lockInfo->Index[j]));

                if (skillId != SKILL_NONE)
                {
                    reqSkillValue = lockInfo->Skill[j];

                    // castitem check: rogue using skeleton keys. the skill values should not be added in this case.
                    skillValue = m_CastItem || m_caster->GetTypeId()!= TYPEID_PLAYER ?
                        0 : m_caster->ToPlayer()->GetSkillValue(skillId);

                    // skill bonus provided by casting spell (mostly item spells)
                    // add the effect base points modifier from the spell cast (cheat lock / skeleton key etc.)
                    if (m_spellInfo->Effects[effIndex].TargetA.GetTarget() == TARGET_GAMEOBJECT_ITEM_TARGET || m_spellInfo->Effects[effIndex].TargetB.GetTarget() == TARGET_GAMEOBJECT_ITEM_TARGET)
                        skillValue += m_spellInfo->Effects[effIndex].CalcValue();

                    if (skillValue < reqSkillValue)
                        return SPELL_FAILED_LOW_CASTLEVEL;
                }

                return SPELL_CAST_OK;
            }
        }
    }

    if (reqKey)
        return SPELL_FAILED_BAD_TARGETS;

    return SPELL_CAST_OK;
}

void Spell::SetSpellValue(SpellValueMod mod, int32 value)
{
    switch (mod)
    {
        case SPELLVALUE_BASE_POINT0:
            m_spellValue->EffectBasePoints[0] = m_spellInfo->Effects[EFFECT_0].CalcBaseValue(value);
            break;
        case SPELLVALUE_BASE_POINT1:
            m_spellValue->EffectBasePoints[1] = m_spellInfo->Effects[EFFECT_1].CalcBaseValue(value);
            break;
        case SPELLVALUE_BASE_POINT2:
            m_spellValue->EffectBasePoints[2] = m_spellInfo->Effects[EFFECT_2].CalcBaseValue(value);
            break;
        case SPELLVALUE_RADIUS_MOD:
            m_spellValue->RadiusMod = (float)value / 10000;
            break;
        case SPELLVALUE_MAX_TARGETS:
            m_spellValue->MaxAffectedTargets = (uint32)value;
            break;
        case SPELLVALUE_AURA_STACK:
            m_spellValue->AuraStackAmount = uint8(value);
            break;
    }
}

void Spell::PrepareTargetProcessing()
{
    CheckEffectExecuteData();
}

void Spell::FinishTargetProcessing()
{
    SendLogExecute();
}

void Spell::InitEffectExecuteData(uint8 effIndex)
{
    ASSERT(effIndex < MAX_SPELL_EFFECTS);
    if (!m_effectExecuteData[effIndex])
    {
        m_effectExecuteData[effIndex] = new ByteBuffer(0x20);
        // first dword - target counter
        *m_effectExecuteData[effIndex] << uint32(1);
    }
    else
    {
        // increase target counter by one
        uint32 count = (*m_effectExecuteData[effIndex]).view_raw<uint32>(0);
        (*m_effectExecuteData[effIndex]).put<uint32>(0, ++count);
    }
}

void Spell::CheckEffectExecuteData()
{
    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        ASSERT(!m_effectExecuteData[i]);
}

void Spell::LoadScripts()
{
    sScriptMgr->CreateSpellScripts(m_spellInfo->Id, m_loadedScripts);
    for (std::list<SpellScript*>::iterator itr = m_loadedScripts.begin(); itr != m_loadedScripts.end();)
    {
        if (!(*itr)->_Load(this))
        {
            std::list<SpellScript*>::iterator bitr = itr;
            ++itr;
            delete (*bitr);
            m_loadedScripts.erase(bitr);
            continue;
        }
        TC_LOG_DEBUG("spells", "Spell::LoadScripts: Script `%s` for spell `%u` is loaded now", (*itr)->_GetScriptName()->c_str(), m_spellInfo->Id);
        (*itr)->Register();
        ++itr;
    }
}

void Spell::CallScriptBeforeCastHandlers()
{
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
    {
        (*scritr)->_PrepareScriptCall(SPELL_SCRIPT_HOOK_BEFORE_CAST);
        std::list<SpellScript::CastHandler>::iterator hookItrEnd = (*scritr)->BeforeCast.end(), hookItr = (*scritr)->BeforeCast.begin();
        for (; hookItr != hookItrEnd; ++hookItr)
            (*hookItr).Call(*scritr);

        (*scritr)->_FinishScriptCall();
    }
}

void Spell::CallScriptOnCastHandlers()
{
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
    {
        (*scritr)->_PrepareScriptCall(SPELL_SCRIPT_HOOK_ON_CAST);
        std::list<SpellScript::CastHandler>::iterator hookItrEnd = (*scritr)->OnCast.end(), hookItr = (*scritr)->OnCast.begin();
        for (; hookItr != hookItrEnd; ++hookItr)
            (*hookItr).Call(*scritr);

        (*scritr)->_FinishScriptCall();
    }
}

void Spell::CallScriptAfterCastHandlers()
{
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
    {
        (*scritr)->_PrepareScriptCall(SPELL_SCRIPT_HOOK_AFTER_CAST);
        std::list<SpellScript::CastHandler>::iterator hookItrEnd = (*scritr)->AfterCast.end(), hookItr = (*scritr)->AfterCast.begin();
        for (; hookItr != hookItrEnd; ++hookItr)
            (*hookItr).Call(*scritr);

        (*scritr)->_FinishScriptCall();
    }
}

SpellCastResult Spell::CallScriptCheckCastHandlers()
{
    SpellCastResult retVal = SPELL_CAST_OK;
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
    {
        (*scritr)->_PrepareScriptCall(SPELL_SCRIPT_HOOK_CHECK_CAST);
        std::list<SpellScript::CheckCastHandler>::iterator hookItrEnd = (*scritr)->OnCheckCast.end(), hookItr = (*scritr)->OnCheckCast.begin();
        for (; hookItr != hookItrEnd; ++hookItr)
        {
            SpellCastResult tempResult = (*hookItr).Call(*scritr);
            if (retVal == SPELL_CAST_OK)
                retVal = tempResult;
        }

        (*scritr)->_FinishScriptCall();
    }
    return retVal;
}

void Spell::PrepareScriptHitHandlers()
{
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
        (*scritr)->_InitHit();
}

bool Spell::CallScriptEffectHandlers(SpellEffIndex effIndex, SpellEffectHandleMode mode)
{
    // execute script effect handler hooks and check if effects was prevented
    bool preventDefault = false;
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
    {
        std::list<SpellScript::EffectHandler>::iterator effItr, effEndItr;
        SpellScriptHookType hookType;
        switch (mode)
        {
            case SPELL_EFFECT_HANDLE_LAUNCH:
                effItr = (*scritr)->OnEffectLaunch.begin();
                effEndItr = (*scritr)->OnEffectLaunch.end();
                hookType = SPELL_SCRIPT_HOOK_EFFECT_LAUNCH;
                break;
            case SPELL_EFFECT_HANDLE_LAUNCH_TARGET:
                effItr = (*scritr)->OnEffectLaunchTarget.begin();
                effEndItr = (*scritr)->OnEffectLaunchTarget.end();
                hookType = SPELL_SCRIPT_HOOK_EFFECT_LAUNCH_TARGET;
                break;
            case SPELL_EFFECT_HANDLE_HIT:
                effItr = (*scritr)->OnEffectHit.begin();
                effEndItr = (*scritr)->OnEffectHit.end();
                hookType = SPELL_SCRIPT_HOOK_EFFECT_HIT;
                break;
            case SPELL_EFFECT_HANDLE_HIT_TARGET:
                effItr = (*scritr)->OnEffectHitTarget.begin();
                effEndItr = (*scritr)->OnEffectHitTarget.end();
                hookType = SPELL_SCRIPT_HOOK_EFFECT_HIT_TARGET;
                break;
            default:
                ABORT();
                return false;
        }
        (*scritr)->_PrepareScriptCall(hookType);
        for (; effItr != effEndItr; ++effItr)
            // effect execution can be prevented
            if (!(*scritr)->_IsEffectPrevented(effIndex) && (*effItr).IsEffectAffected(m_spellInfo, effIndex))
                (*effItr).Call(*scritr, effIndex);

        if (!preventDefault)
            preventDefault = (*scritr)->_IsDefaultEffectPrevented(effIndex);

        (*scritr)->_FinishScriptCall();
    }
    return preventDefault;
}

void Spell::CallScriptSuccessfulDispel(SpellEffIndex effIndex)
{
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
    {
        (*scritr)->_PrepareScriptCall(SPELL_SCRIPT_HOOK_EFFECT_SUCCESSFUL_DISPEL);
        std::list<SpellScript::EffectHandler>::iterator hookItrEnd = (*scritr)->OnEffectSuccessfulDispel.end(), hookItr = (*scritr)->OnEffectSuccessfulDispel.begin();
        for (; hookItr != hookItrEnd; ++hookItr)
            hookItr->Call(*scritr, effIndex);

        (*scritr)->_FinishScriptCall();
    }
}

void Spell::CallScriptBeforeHitHandlers()
{
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
    {
        (*scritr)->_PrepareScriptCall(SPELL_SCRIPT_HOOK_BEFORE_HIT);
        std::list<SpellScript::HitHandler>::iterator hookItrEnd = (*scritr)->BeforeHit.end(), hookItr = (*scritr)->BeforeHit.begin();
        for (; hookItr != hookItrEnd; ++hookItr)
            (*hookItr).Call(*scritr);

        (*scritr)->_FinishScriptCall();
    }
}

void Spell::CallScriptOnHitHandlers()
{
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
    {
        (*scritr)->_PrepareScriptCall(SPELL_SCRIPT_HOOK_HIT);
        std::list<SpellScript::HitHandler>::iterator hookItrEnd = (*scritr)->OnHit.end(), hookItr = (*scritr)->OnHit.begin();
        for (; hookItr != hookItrEnd; ++hookItr)
            (*hookItr).Call(*scritr);

        (*scritr)->_FinishScriptCall();
    }
}

void Spell::CallScriptAfterHitHandlers()
{
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
    {
        (*scritr)->_PrepareScriptCall(SPELL_SCRIPT_HOOK_AFTER_HIT);
        std::list<SpellScript::HitHandler>::iterator hookItrEnd = (*scritr)->AfterHit.end(), hookItr = (*scritr)->AfterHit.begin();
        for (; hookItr != hookItrEnd; ++hookItr)
            (*hookItr).Call(*scritr);

        (*scritr)->_FinishScriptCall();
    }
}

void Spell::CallScriptObjectAreaTargetSelectHandlers(std::list<WorldObject*>& targets, SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType)
{
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
    {
        (*scritr)->_PrepareScriptCall(SPELL_SCRIPT_HOOK_OBJECT_AREA_TARGET_SELECT);
        std::list<SpellScript::ObjectAreaTargetSelectHandler>::iterator hookItrEnd = (*scritr)->OnObjectAreaTargetSelect.end(), hookItr = (*scritr)->OnObjectAreaTargetSelect.begin();
        for (; hookItr != hookItrEnd; ++hookItr)
            if (hookItr->IsEffectAffected(m_spellInfo, effIndex) && targetType.GetTarget() == hookItr->GetTarget())
                hookItr->Call(*scritr, targets);

        (*scritr)->_FinishScriptCall();
    }
}

void Spell::CallScriptObjectTargetSelectHandlers(WorldObject*& target, SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType)
{
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
    {
        (*scritr)->_PrepareScriptCall(SPELL_SCRIPT_HOOK_OBJECT_TARGET_SELECT);
        std::list<SpellScript::ObjectTargetSelectHandler>::iterator hookItrEnd = (*scritr)->OnObjectTargetSelect.end(), hookItr = (*scritr)->OnObjectTargetSelect.begin();
        for (; hookItr != hookItrEnd; ++hookItr)
            if (hookItr->IsEffectAffected(m_spellInfo, effIndex) && targetType.GetTarget() == hookItr->GetTarget())
                hookItr->Call(*scritr, target);

        (*scritr)->_FinishScriptCall();
    }
}

void Spell::CallScriptDestinationTargetSelectHandlers(SpellDestination& target, SpellEffIndex effIndex, SpellImplicitTargetInfo const& targetType)
{
    for (std::list<SpellScript*>::iterator scritr = m_loadedScripts.begin(); scritr != m_loadedScripts.end(); ++scritr)
    {
        (*scritr)->_PrepareScriptCall(SPELL_SCRIPT_HOOK_DESTINATION_TARGET_SELECT);
        std::list<SpellScript::DestinationTargetSelectHandler>::iterator hookItrEnd = (*scritr)->OnDestinationTargetSelect.end(), hookItr = (*scritr)->OnDestinationTargetSelect.begin();
        for (; hookItr != hookItrEnd; ++hookItr)
            if (hookItr->IsEffectAffected(m_spellInfo, effIndex) && targetType.GetTarget() == hookItr->GetTarget())
                hookItr->Call(*scritr, target);

        (*scritr)->_FinishScriptCall();
    }
}

bool Spell::CheckScriptEffectImplicitTargets(uint32 effIndex, uint32 effIndexToCheck)
{
    // Skip if there are not any script
    if (!m_loadedScripts.size())
        return true;

    for (std::list<SpellScript*>::iterator itr = m_loadedScripts.begin(); itr != m_loadedScripts.end(); ++itr)
    {
        std::list<SpellScript::ObjectTargetSelectHandler>::iterator targetSelectHookEnd = (*itr)->OnObjectTargetSelect.end(), targetSelectHookItr = (*itr)->OnObjectTargetSelect.begin();
        for (; targetSelectHookItr != targetSelectHookEnd; ++targetSelectHookItr)
            if (((*targetSelectHookItr).IsEffectAffected(m_spellInfo, effIndex) && !(*targetSelectHookItr).IsEffectAffected(m_spellInfo, effIndexToCheck)) ||
                (!(*targetSelectHookItr).IsEffectAffected(m_spellInfo, effIndex) && (*targetSelectHookItr).IsEffectAffected(m_spellInfo, effIndexToCheck)))
                return false;

        std::list<SpellScript::ObjectAreaTargetSelectHandler>::iterator areaTargetSelectHookEnd = (*itr)->OnObjectAreaTargetSelect.end(), areaTargetSelectHookItr = (*itr)->OnObjectAreaTargetSelect.begin();
        for (; areaTargetSelectHookItr != areaTargetSelectHookEnd; ++areaTargetSelectHookItr)
            if (((*areaTargetSelectHookItr).IsEffectAffected(m_spellInfo, effIndex) && !(*areaTargetSelectHookItr).IsEffectAffected(m_spellInfo, effIndexToCheck)) ||
                (!(*areaTargetSelectHookItr).IsEffectAffected(m_spellInfo, effIndex) && (*areaTargetSelectHookItr).IsEffectAffected(m_spellInfo, effIndexToCheck)))
                return false;
    }
    return true;
}

bool Spell::CanExecuteTriggersOnHit(uint8 effMask, SpellInfo const* triggeredByAura) const
{
    bool only_on_caster = (triggeredByAura && triggeredByAura->HasAttribute(SPELL_ATTR4_PROC_ONLY_ON_CASTER));
    // If triggeredByAura has SPELL_ATTR4_PROC_ONLY_ON_CASTER then it can only proc on a cast spell with TARGET_UNIT_CASTER
    for (uint8 i = 0;i < MAX_SPELL_EFFECTS; ++i)
    {
        if ((effMask & (1 << i)) && (!only_on_caster || (m_spellInfo->Effects[i].TargetA.GetTarget() == TARGET_UNIT_CASTER)))
            return true;
    }
    return false;
}

void Spell::PrepareTriggersExecutedOnHit()
{
    /// @todo move this to scripts
    if (m_spellInfo->SpellFamilyName)
    {
        SpellInfo const* excludeCasterSpellInfo = sSpellMgr->GetSpellInfo(m_spellInfo->ExcludeCasterAuraSpell);
        if (excludeCasterSpellInfo && !excludeCasterSpellInfo->IsPositive())
            m_preCastSpell = m_spellInfo->ExcludeCasterAuraSpell;
        SpellInfo const* excludeTargetSpellInfo = sSpellMgr->GetSpellInfo(m_spellInfo->ExcludeTargetAuraSpell);
        if (excludeTargetSpellInfo && !excludeTargetSpellInfo->IsPositive())
            m_preCastSpell = m_spellInfo->ExcludeTargetAuraSpell;
    }

    /// @todo move this to scripts
    switch (m_spellInfo->SpellFamilyName)
    {
        case SPELLFAMILY_MAGE:
        {
             // Permafrost
             if (m_spellInfo->SpellFamilyFlags[1] & 0x00001000 ||  m_spellInfo->SpellFamilyFlags[0] & 0x00100220)
                 m_preCastSpell = 68391;
             break;
        }
    }

    // handle SPELL_AURA_ADD_TARGET_TRIGGER auras:
    // save auras which were present on spell caster on cast, to prevent triggered auras from affecting caster
    // and to correctly calculate proc chance when combopoints are present
    auto const& targetTriggers = m_caster->GetAuraEffectsByType(SPELL_AURA_ADD_TARGET_TRIGGER);
    for (auto aura : targetTriggers)
    {
        if (!aura->IsAffectedOnSpell(m_spellInfo))
            continue;
        SpellInfo const* auraSpellInfo = aura->GetSpellInfo();
        uint32 auraSpellIdx = aura->GetEffIndex();
        if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(auraSpellInfo->Effects[auraSpellIdx].TriggerSpell))
        {
            // calculate the chance using spell base amount, because aura amount is not updated on combo-points change
            // this possibly needs fixing
            int32 auraBaseAmount = aura->GetBaseAmount();
            // proc chance is stored in effect amount
            int32 chance = m_caster->CalculateSpellDamage(NULL, auraSpellInfo, auraSpellIdx, &auraBaseAmount);
            if (chance < 0)
                chance = auraSpellInfo->ProcChance;
            // build trigger and add to the list
            HitTriggerSpell spellTriggerInfo;
            spellTriggerInfo.triggeredSpell = spellInfo;
            spellTriggerInfo.triggeredByAura = auraSpellInfo;
            spellTriggerInfo.triggeredByItemGUID = aura->GetBase()->GetCastItemGUID();
            spellTriggerInfo.chance = chance * aura->GetBase()->GetStackAmount();
            m_hitTriggerSpells.push_back(spellTriggerInfo);
        }
    }
}

// Global cooldowns management
enum GCDLimits
{
    MIN_GCD = 1000,
    MAX_GCD = 1500
};

bool Spell::HasGlobalCooldown() const
{
    // Only players or controlled units have global cooldown
    if (m_caster->GetTypeId() != TYPEID_PLAYER && !m_caster->GetCharmInfo())
        return false;

    return m_caster->GetSpellHistory()->HasGlobalCooldown(m_spellInfo);
}

void Spell::TriggerGlobalCooldown()
{
    int32 gcd = m_spellInfo->StartRecoveryTime;
    if (!gcd)
        return;

    // Only players or controlled units have global cooldown
    if (m_caster->GetTypeId() != TYPEID_PLAYER && !m_caster->GetCharmInfo())
        return;

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
        if (m_caster->ToPlayer()->GetCommandStatus(CHEAT_COOLDOWN))
            return;

    // Global cooldown can't leave range 1..1.5 secs
    // There are some spells (mostly not cast directly by player) that have < 1 sec and > 1.5 sec global cooldowns
    // but as tests show are not affected by any spell mods.
    if (m_spellInfo->StartRecoveryTime >= MIN_GCD && m_spellInfo->StartRecoveryTime <= MAX_GCD)
    {
        // gcd modifier auras are applied only to own spells and only players have such mods
        if (m_caster->GetTypeId() == TYPEID_PLAYER)
            m_caster->ToPlayer()->ApplySpellMod(m_spellInfo->Id, SPELLMOD_GLOBAL_COOLDOWN, gcd, this);

        // Apply haste rating
        gcd = int32(float(gcd) * m_caster->GetFloatValue(UNIT_MOD_CAST_SPEED));
        if (gcd < MIN_GCD)
            gcd = MIN_GCD;
        else if (gcd > MAX_GCD)
            gcd = MAX_GCD;
    }

    m_caster->GetSpellHistory()->AddGlobalCooldown(m_spellInfo, gcd);
}

void Spell::CancelGlobalCooldown()
{
    if (!m_spellInfo->StartRecoveryTime)
        return;

    // Cancel global cooldown when interrupting current cast
    if (m_caster->GetCurrentSpell(CURRENT_GENERIC_SPELL) != this)
        return;

    // Only players or controlled units have global cooldown
    if (m_caster->GetTypeId() != TYPEID_PLAYER && !m_caster->GetCharmInfo())
        return;

    m_caster->GetSpellHistory()->CancelGlobalCooldown(m_spellInfo);
}

/**
* @brief Calculates and chaches the bonus (flat/multiplicative) for SpellMod-Types that are important when a delayed spell hits. 
* Caching is needed for delayed spells because spellmods and their auras are removed after releasing the spell,
* but still take effect when the spell hits later.
* This is only important for NOT-spell-triggered spells as triggered spells let their triggering spell consume the charges for them.
* Not evaluated SpellModOps are: Casting-Time, cost, range and global cooldown. They all took effect before the spell
* was released
*/
void Spell::CacheSpellModsForDelayedHit()
{
    // Spells triggered by other spells don't need a cache for spell mods
    if (IsTriggered() && m_triggeredByAuraSpell == NULL)
        return;

    Player* modOwner = m_caster->GetSpellModOwner();
    if (!modOwner)
        return;

    for (uint8 i = 0; i < MAX_SPELLMOD; ++i)
    {
        // These mod types are handled before this function and shouldn't be handled twice 
        // to avoid consuming later procced mods
        if (i == SPELLMOD_CASTING_TIME ||
            i == SPELLMOD_COST ||
            i == SPELLMOD_RANGE ||
            i == SPELLMOD_GLOBAL_COOLDOWN)
            continue;

        int basevalue = 1;
        // Apply spell mod without using cached values: They are created here
        Unit::FlatAndPercentModifiers modValues = modOwner->ApplySpellMod(m_spellInfo->Id, (SpellModOp)i, basevalue, this, false);
        // only cache for this SpellModOp if there exist a value changing mod (if the values are not the default 0 flat, 1.00 percent multiplier)
        // if it is the default values we don't need to store it because we can just return it in case we don't find anything else in the map
        if (modValues.first != 0 || modValues.second != 1.0f)
            m_spellModCache[i] = modValues;
    }
}

/**
* @brief Returns the values (flat and muliplicative bonus) for the given SpellModOp from the spellModCache of this spell
*/
Unit::FlatAndPercentModifiers Spell::GetCachedSpellModsForOp(SpellModOp op)
{
    std::map<uint8, std::pair<int32, float>>::iterator modFinder = m_spellModCache.find(op);
    if (modFinder != m_spellModCache.end())
        return modFinder->second;
    else //we didn't cache default values, so in this case we must return (0,1.0f) (see CacheSpellModsForDelayedHit())
        return Unit::FlatAndPercentModifiers(0, 1.0f);
}

void Spell::ResetAttackTimerAfterCastIfNeeded()
{
    if (IsAutoActionResetSpell())
    {
        bool found = false;
        Unit::AuraEffectList const& vIgnoreReset = m_caster->GetAuraEffectsByType(SPELL_AURA_IGNORE_MELEE_RESET);
        for (Unit::AuraEffectList::const_iterator i = vIgnoreReset.begin(); i != vIgnoreReset.end(); ++i)
        {
            if ((*i)->IsAffectedOnSpell(m_spellInfo))
            {
                found = true;
                break;
            }
        }

        if (m_spellInfo->CalcCastTime() > 0 && !GetCastTime())
            found = true;
        if (!found && !m_spellInfo->HasAttribute(SPELL_ATTR2_NOT_RESET_AUTO_ACTIONS))
        {
            m_caster->resetAttackTimer(BASE_ATTACK);
            if (m_caster->haveOffhandWeapon())
                m_caster->resetAttackTimer(OFF_ATTACK);
            m_caster->resetAttackTimer(RANGED_ATTACK);
        }
    }
}

void Spell::AddUsedModAura(Aura* usedAura)
{
    usedAura->spellUsingModsFromThisAura.push_front(this);
    m_appliedMods.insert(usedAura);
}

void Spell::RemoveUsedModAura(Aura* usedAura)
{
    usedAura->spellUsingModsFromThisAura.remove(this);
    m_appliedMods.erase(usedAura);
}

Unit* Spell::GetUnitTarget() const
{
    if (Unit* target = m_targets.GetUnitTarget())
        return target;

    return unitTarget;
}

/*
* @brief Drops a charge for all spellmod-auras that were used by this spell
* @param finishedCasting if set to true also auras with SPELL_ATTR0_CU_KEEP_SPELLMOD_WHILE_CASTING
* will loose one charge. Should be set to true only in the finish phase (finish()) of the spell
*/
void Spell::DropUsedModAurasCharges(bool finishedCasting)
{
    if (m_appliedMods.empty())
        return;
    std::set<Aura*> usedAurasCopy = m_appliedMods;
    for (Aura* usedAura : usedAurasCopy)
    {
        if ((!finishedCasting || IsTriggered()) && usedAura->GetSpellInfo()->AttributesCu & SPELL_ATTR0_CU_KEEP_SPELLMOD_WHILE_CASTING)
            continue;

        usedAura->DropCharge(AURA_REMOVE_BY_EXPIRE);
        RemoveUsedModAura(usedAura);
    }

}

/*
* @brief Restores spellmods-auras for a cast by removing them from the list
* of the auras we will  drop charges from. Won't do anything if the aura-charge 
* was already dropped via DropUsedModAurasCharges()
* @param ownerAuraId if set, only spellmods of auras with this id will be restored
* @param auraToSet if set, only spellmods of exactly this auras will be restored
*/
void Spell::RestoreUsedModAuras(uint32 ownerAuraId, Aura* auraToRestore)
{
    if (m_appliedMods.empty())
        return;

    std::set<Aura*> usedAuraCopy = m_appliedMods;
    for (Aura* usedAura : usedAuraCopy)
    {
        if (!ownerAuraId || usedAura->GetSpellInfo()->Id == ownerAuraId)
            if (!auraToRestore || usedAura == auraToRestore)
                RemoveUsedModAura(usedAura);
    }
}

/*
* @brief Declare that a mod was used by the spell. All mods (more precise their auras) used by a spell
* will later loose a charge (as soon as DropUsedModAurasCharges() gets called at the end of cast() or finish())
* @param mod a pointer to the SpellModifier which was used, nothing happens if it is null
*/
void Spell::AddAppliedMod(SpellModifier* mod)
{
    // don't handle spells with proc_event entry defined
    // this is a temporary workaround, because all spellmods should be handled like that
    if (!mod || sSpellMgr->GetSpellProcEvent(mod->spellId))
        return;
    Aura* modAura = mod->ownerAura; // Can be null if mod has no aura
    if (IsTriggered() && modAura && (modAura->GetSpellInfo()->AttributesCu & SPELL_ATTR0_CU_KEEP_SPELLMOD_WHILE_CASTING))
        return;
    if (modAura && modAura->IsUsingCharges())
    {
        AddUsedModAura(modAura);
    }
}

/*
* @brief Finds cached spell mods. This is important for delayed spells
* because at the time they hit, the mod-auras might have already been removed.
* @param modValues a reference to a int/float pair, it gets the found flat/percentage modifiers assigned
* @param op the SpellModOp we want to know the values for
* @param modTakingSpellId the id of the spell that should profit from the mods. If its id is not the same as this spell
* false will be returned because the cached mods only fit to similar spells
* @return true if this spell has cached mods and the id is equal to the modTakingSpellId. In this case
* the found cached values are assigned to modValues. false otherwise
* @todo: make the modTakingSpellId unnecessary by making sure that this is not called by triggering spells on its
* trigger spell, which currently happens in the call in applyspellmod in player
*/
bool Spell::FindCachedSpellMods(std::pair<int32, float>& modValues, SpellModOp op, uint32 modTakingSpellId)
{
    if (HasCachedMods() && GetSpellInfo()->Id == modTakingSpellId)
    {
        modValues = GetCachedSpellModsForOp(op);
        return true;
    }
    return false;
}

std::string Spell::OnQueryOwnerName() const
{
    return std::to_string(m_spellInfo->Id) + '_' + std::to_string(reinterpret_cast<std::uintptr_t>(this));
}

std::string Spell::OnQueryFullName() const
{
    return std::string("\"") + m_spellInfo->SpellName[0] + "\" (Spell: " + std::to_string(m_spellInfo->Id) + ", Caster: \"" + m_caster->GetName() + "\")";
}

bool Spell::OnInitScript(std::string_view scriptName)
{
    return loadScriptType(*this, scriptName);
}

namespace Trinity
{

WorldObjectSpellTargetCheck::WorldObjectSpellTargetCheck(Unit* caster, Unit* referer, SpellInfo const* spellInfo,
            SpellTargetCheckTypes selectionType, ConditionContainer* condList) : _caster(caster), _referer(referer), _spellInfo(spellInfo),
    _targetSelectionType(selectionType), _condList(condList)
{
    if (condList)
        _condSrcInfo = new ConditionSourceInfo(NULL, caster);
    else
        _condSrcInfo = NULL;
}

WorldObjectSpellTargetCheck::~WorldObjectSpellTargetCheck()
{
    delete _condSrcInfo;
}

bool WorldObjectSpellTargetCheck::operator()(WorldObject* target)
{
    if (_spellInfo->CheckTarget(_caster, target, true) != SPELL_CAST_OK)
        return false;
    Unit* unitTarget = target->ToUnit();
    if (Corpse* corpseTarget = target->ToCorpse())
    {
        // use ofter for party/assistance checks
        if (Player* owner = ObjectAccessor::FindPlayer(corpseTarget->GetOwnerGUID()))
            unitTarget = owner;
        else
            return false;
    }
    if (unitTarget)
    {
        switch (_targetSelectionType)
        {
            case TARGET_CHECK_ENEMY:
                if (unitTarget->IsTotem())
                    return false;
                if (!_caster->_IsValidAttackTarget(unitTarget, _spellInfo))
                    return false;
                break;
            case TARGET_CHECK_ALLY:
                if (unitTarget->IsTotem())
                    return false;
                if (!_caster->_IsValidAssistTarget(unitTarget, _spellInfo))
                    return false;
                break;
            case TARGET_CHECK_PARTY:
                if (unitTarget->IsTotem())
                    return false;
                if (!_caster->_IsValidAssistTarget(unitTarget, _spellInfo))
                    return false;
                if (!_referer->IsInPartyWith(unitTarget))
                    return false;
                break;
            case TARGET_CHECK_RAID_CLASS:
                if (_referer->getClass() != unitTarget->getClass())
                    return false;
                // nobreak;
            case TARGET_CHECK_RAID:
                if (unitTarget->IsTotem())
                    return false;
                if (!_caster->_IsValidAssistTarget(unitTarget, _spellInfo))
                    return false;
                if (!_referer->IsInRaidWith(unitTarget))
                    return false;
                break;
            case TARGET_CHECK_CORPSE:
                if (_caster->IsFriendlyTo(unitTarget))
                    return false;
                break;
            default:
                break;
        }
    }
    if (!_condSrcInfo)
        return true;
    _condSrcInfo->mConditionTargets[0] = target;
    return sConditionMgr->IsObjectMeetToConditions(*_condSrcInfo, *_condList);
}

WorldObjectSpellNearbyTargetCheck::WorldObjectSpellNearbyTargetCheck(float range, Unit* caster, SpellInfo const* spellInfo,
    SpellTargetCheckTypes selectionType, ConditionContainer* condList)
    : WorldObjectSpellTargetCheck(caster, caster, spellInfo, selectionType, condList), _range(range), _position(caster) { }

bool WorldObjectSpellNearbyTargetCheck::operator()(WorldObject* target)
{
    float dist = target->GetDistance(*_position);
    if (dist < _range && WorldObjectSpellTargetCheck::operator ()(target))
    {
        _range = dist;
        return true;
    }
    return false;
}

WorldObjectSpellAreaTargetCheck::WorldObjectSpellAreaTargetCheck(float range, Position const* position, Unit* caster,
    Unit* referer, SpellInfo const* spellInfo, SpellTargetCheckTypes selectionType, ConditionContainer* condList)
    : WorldObjectSpellTargetCheck(caster, referer, spellInfo, selectionType, condList), _range(range), _position(position) { }

bool WorldObjectSpellAreaTargetCheck::operator()(WorldObject* target)
{
    if (!target->IsWithinDist3d(_position, _range) && !(target->ToGameObject() && target->ToGameObject()->IsInRange(_position->GetPositionX(), _position->GetPositionY(), _position->GetPositionZ(), _range)))
        return false;
    return WorldObjectSpellTargetCheck::operator ()(target);
}

WorldObjectSpellConeTargetCheck::WorldObjectSpellConeTargetCheck(float coneAngle, float range, Unit* caster,
    SpellInfo const* spellInfo, SpellTargetCheckTypes selectionType, ConditionContainer* condList)
    : WorldObjectSpellAreaTargetCheck(range, caster, caster, caster, spellInfo, selectionType, condList), _coneAngle(coneAngle) { }

bool WorldObjectSpellConeTargetCheck::operator()(WorldObject* target)
{
    if (_spellInfo->HasAttribute(SPELL_ATTR0_CU_CONE_BACK))
    {
        if (!_caster->isInBack(target, _coneAngle))
            return false;
    }
    else if (_spellInfo->HasAttribute(SPELL_ATTR0_CU_CONE_LINE))
    {
        if (!_caster->HasInLine(target, _caster->GetObjectSize()))
            return false;
    }
    else
    {
        // The effect will only hit targets that are on your horizontal plane, meaning it does not shoot uphill or downhill. 
        if (std::abs(_caster->GetPositionZ() - target->GetPositionZ()) > (_caster->GetObjectSize() + target->GetObjectSize()) * 1.5f)
            return false;

        if (!_caster->isInFront(target, _coneAngle))
            return false;
    }
    return WorldObjectSpellAreaTargetCheck::operator ()(target);
}

WorldObjectSpellTrajTargetCheck::WorldObjectSpellTrajTargetCheck(float range, Position const* position, Unit* caster, SpellInfo const* spellInfo)
    : WorldObjectSpellAreaTargetCheck(range, position, caster, caster, spellInfo, TARGET_CHECK_DEFAULT, NULL) { }

bool WorldObjectSpellTrajTargetCheck::operator()(WorldObject* target)
{
    // return all targets on missile trajectory (0 - size of a missile)
    if (!_caster->HasInLine(target, 0))
        return false;
    return WorldObjectSpellAreaTargetCheck::operator ()(target);
}

} //namespace Trinity
