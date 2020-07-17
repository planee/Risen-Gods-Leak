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
SDName: Npc_Professions
SD%Complete: 80
SDComment: Provides learn/unlearn/relearn-options for professions. Not supported: Unlearn engineering, re-learn engineering, re-learn leatherworking.
SDCategory: NPCs
EndScriptData */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Player.h"
#include "SpellInfo.h"
#include "WorldSession.h"

/*
A few notes for future developement:
- A full implementation of gossip for GO's is required. They must have the same scripting capabilities as creatures. Basically,
there is no difference here (except that default text is chosen with `gameobject_template`.`data3` (for GO type2, different dataN for a few others)
- It's possible blacksmithing still require some tweaks and adjustments due to the way we _have_ to use reputation.
*/

/*###
# to be removed from here (->ncp_text). This is data for database projects.
###*/
#define TALK_MUST_UNLEARN_WEAPON    "You must forget your weapon type specialty before I can help you. Go to Everlook in Winterspring and seek help there."

#define TALK_HAMMER_LEARN           "Ah, a seasoned veteran you once were. I know you are capable, you merely need to ask and I shall teach you the way of the hammersmith."
#define TALK_AXE_LEARN              "Ah, a seasoned veteran you once were. I know you are capable, you merely need to ask and I shall teach you the way of the axesmith."
#define TALK_SWORD_LEARN            "Ah, a seasoned veteran you once were. I know you are capable, you merely need to ask and I shall teach you the way of the swordsmith."

#define TALK_HAMMER_UNLEARN         "Forgetting your Hammersmithing skill is not something to do lightly. If you choose to abandon it you will forget all recipes that require Hammersmithing to create!"
#define TALK_AXE_UNLEARN            "Forgetting your Axesmithing skill is not something to do lightly. If you choose to abandon it you will forget all recipes that require Axesmithing to create!"
#define TALK_SWORD_UNLEARN          "Forgetting your Swordsmithing skill is not something to do lightly. If you choose to abandon it you will forget all recipes that require Swordsmithing to create!"
#define TALK_ENGINEER_SPEC_LEARN    "Hundreds of various diagrams and schematics begin to take shape on the pages of the book. You recognize some of the diagrams while others remain foreign but familiar."

/*###
# npc_text defines
###*/

#define TALK_TRANSMUTE_UNLEARN    11076
#define TALK_ELIXIR_UNLEARN       11075
#define TALK_ELEMENTAL_UNLEARN    10302
#define TALK_LEATHER_SPEC_LEARN   8326

/*###
# generic defines
###*/

#define GOSSIP_SENDER_LEARN         50
#define GOSSIP_SENDER_UNLEARN       51
#define GOSSIP_SENDER_CHECK         52

/*###
# gossip item and box texts
###*/


#define GOSSIP_LEARN_DRAGON         "I am absolutely certain that i want to learn dragonscale leatherworking"
#define GOSSIP_LEARN_ELEMENTAL      "I am absolutely certain that i want to learn elemental leatherworking"
#define GOSSIP_LEARN_TRIBAL         "I am absolutely certain that i want to learn tribal leatherworking"
#define GOSSIP_LEARN_POTION         "Please teach me how to become a Master of Potions, Lauranna"
#define GOSSIP_UNLEARN_POTION       "I wish to unlearn Potion Mastery"
#define GOSSIP_LEARN_TRANSMUTE      "Please teach me how to become a Master of Transmutations, Zarevhi"
#define GOSSIP_UNLEARN_TRANSMUTE    "I wish to unlearn Transmutation Mastery"
#define GOSSIP_LEARN_ELIXIR         "Please teach me how to become a Master of Elixirs, Lorokeem"
#define GOSSIP_UNLEARN_ELIXIR       "I wish to unlearn Elixir Mastery"

#define BOX_UNLEARN_ALCHEMY_SPEC    "Do you really want to unlearn your alchemy specialty and lose all associated recipes? \n Cost: "

#define GOSSIP_WEAPON_LEARN         "Please teach me how to become a Weaponsmith"
#define GOSSIP_WEAPON_UNLEARN       "I wish to unlearn the art of Weaponsmithing"
#define GOSSIP_ARMOR_LEARN          "Please teach me how to become a Armorsmith"
#define GOSSIP_ARMOR_UNLEARN        "I wish to unlearn the art of Armorsmithing"

#define GOSSIP_UNLEARN_SMITH_SPEC   "I wish to unlearn my blacksmith specialty"
#define BOX_UNLEARN_ARMORORWEAPON   "Do you really want to unlearn your blacksmith specialty and lose all associated recipes? \n Cost: "

#define GOSSIP_LEARN_HAMMER         "Please teach me how to become a Hammersmith, Lilith"
#define GOSSIP_UNLEARN_HAMMER       "I wish to unlearn Hammersmithing"
#define GOSSIP_LEARN_AXE            "Please teach me how to become a Axesmith, Kilram"
#define GOSSIP_UNLEARN_AXE          "I wish to unlearn Axesmithing"
#define GOSSIP_LEARN_SWORD          "Please teach me how to become a Swordsmith, Seril"
#define GOSSIP_UNLEARN_SWORD        "I wish to unlearn Swordsmithing"

#define BOX_UNLEARN_WEAPON_SPEC     "Do you really want to unlearn your weaponsmith specialty and lose all associated recipes? \n Cost: "

#define GOSSIP_UNLEARN_DRAGON       "I wish to unlearn Dragonscale Leatherworking"
#define GOSSIP_UNLEARN_ELEMENTAL    "I wish to unlearn Elemental Leatherworking"
#define GOSSIP_UNLEARN_TRIBAL       "I wish to unlearn Tribal Leatherworking"

#define BOX_UNLEARN_LEATHER_SPEC    "Do you really want to unlearn your leatherworking specialty and lose all associated recipes? \n Cost: "

#define GOSSIP_LEARN_SPELLFIRE      "Please teach me how to become a Spellcloth tailor"
#define GOSSIP_UNLEARN_SPELLFIRE    "I wish to unlearn Spellfire Tailoring"
#define GOSSIP_LEARN_MOONCLOTH      "Please teach me how to become a Mooncloth tailor"
#define GOSSIP_UNLEARN_MOONCLOTH    "I wish to unlearn Mooncloth Tailoring"
#define GOSSIP_LEARN_SHADOWEAVE     "Please teach me how to become a Shadoweave tailor"
#define GOSSIP_UNLEARN_SHADOWEAVE   "I wish to unlearn Shadoweave Tailoring"

#define BOX_UNLEARN_TAILOR_SPEC     "Do you really want to unlearn your tailoring specialty and lose all associated recipes? \n Cost: "

#define GOSSIP_LEARN_GOBLIN         "I am 100% confident that I wish to learn in the ways of goblin engineering."
#define GOSSIP_UNLEARN_GOBLIN       "I wish to unlearn my Goblin Engineering specialization!"
#define GOSSIP_LEARN_GNOMISH        "I am 100% confident that I wish to learn in the ways of gnomish engineering."
#define GOSSIP_UNLEARN_GNOMISH      "I wish to unlearn my Gnomish Engineering specialization!"

#define BOX_UNLEARN_GOBLIN_SPEC     "Do you really want to unlearn your Goblin Engineering specialization and lose all asociated recipes?"
#define BOX_UNLEARN_GNOMISH_SPEC    "Do you really want to unlearn your Gnomish Engineering specialization and lose all asociated recipes?"

/*###
# spells defines
###*/
enum ProfessionSpells
{
    S_WEAPON                = 9787,
    S_ARMOR                 = 9788,
    S_HAMMER                = 17040,
    S_AXE                   = 17041,
    S_SWORD                 = 17039,

    S_LEARN_WEAPON          = 9789,
    S_LEARN_ARMOR           = 9790,
    S_LEARN_HAMMER          = 39099,
    S_LEARN_AXE             = 39098,
    S_LEARN_SWORD           = 39097,

    S_UNLEARN_WEAPON        = 36436,
    S_UNLEARN_ARMOR         = 36435,
    S_UNLEARN_HAMMER        = 36441,
    S_UNLEARN_AXE           = 36439,
    S_UNLEARN_SWORD         = 36438,

    S_REP_ARMOR             = 17451,
    S_REP_WEAPON            = 17452,

    REP_ARMOR               = 46,
    REP_WEAPON              = 289,
    REP_HAMMER              = 569,
    REP_AXE                 = 570,
    REP_SWORD               = 571,

    S_DRAGON                = 10656,
    S_ELEMENTAL             = 10658,
    S_TRIBAL                = 10660,

    S_LEARN_DRAGON          = 10657,
    S_LEARN_ELEMENTAL       = 10659,
    S_LEARN_TRIBAL          = 10661,

    S_UNLEARN_DRAGON        = 36434,
    S_UNLEARN_ELEMENTAL     = 36328,
    S_UNLEARN_TRIBAL        = 36433,

    S_GOBLIN                = 20222,
    S_GNOMISH               = 20219,

    S_LEARN_GOBLIN          = 20221,
    S_LEARN_GNOMISH         = 20220,

    S_UNLEARN_GOBLIN        = 68334,
    S_UNLEARN_GNOMISH       = 68333,

    S_SPELLFIRE             = 26797,
    S_MOONCLOTH             = 26798,
    S_SHADOWEAVE            = 26801,

    S_LEARN_SPELLFIRE       = 26796,
    S_LEARN_MOONCLOTH       = 26799,
    S_LEARN_SHADOWEAVE      = 26800,

    S_UNLEARN_SPELLFIRE     = 41299,
    S_UNLEARN_MOONCLOTH     = 41558,
    S_UNLEARN_SHADOWEAVE    = 41559,

    S_TRANSMUTE             = 28672,
    S_ELIXIR                = 28677,
    S_POTION                = 28675,

    S_LEARN_TRANSMUTE       = 28674,
    S_LEARN_ELIXIR          = 28678,
    S_LEARN_POTION          = 28676,

    S_UNLEARN_TRANSMUTE     = 41565,
    S_UNLEARN_ELIXIR        = 41564,
    S_UNLEARN_POTION        = 41563,
};

enum Quests
{
    QUEST_TRIBAL_TEATHERWORKING      = 5143,
    QUEST_ELEMENTAL_LEATHERWORKING   = 5144,
    QUEST_DRAGONSCALE_LEATHERWORKING = 5145,
    QUEST_SHOW_YOUR_WORK_1           = 3643,
    QUEST_SHOW_YOUR_WORK_2           = 3641,
    QUEST_SHOW_YOUR_WORK_3           = 3639,
};

/*###
# formulas to calculate unlearning cost
###*/

int32 DoLearnCost(Player* /*player*/)                      //tailor, alchemy
{
    return 200000;
}

int32 DoHighUnlearnCost(Player* /*player*/)                //tailor, alchemy
{
    return 1500000;
}

int32 DoMedUnlearnCost(Player* player)                     //blacksmith, leatherwork
{
    uint8 level = player->getLevel();
    if (level < 51)
        return 250000;
    else if (level < 66)
        return 500000;
    else
        return 1000000;
}

int32 DoLowUnlearnCost(Player* player)                     //blacksmith
{
    uint8 level = player->getLevel();
    if (level < 66)
        return 50000;
    else
        return 100000;
}

void ProcessCastaction(Player* player, Creature* creature, uint32 spellId, uint32 triggeredSpellId, int32 cost)
{
    if (!(spellId && player->HasSpell(spellId)) && player->HasEnoughMoney(cost))
    {
        player->CastSpell(player, triggeredSpellId, true);
        player->ModifyMoney(-cost);
    }
    else
        player->SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, creature, 0, 0);
    player->CLOSE_GOSSIP_MENU();
}

/*###
# unlearning related profession spells
###*/

bool EquippedOk(Player* player, uint32 spellId)
{
    SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId);
    if (!spell)
        return false;

    for (uint8 i = 0; i < 3; ++i)
    {
        uint32 reqSpell = spell->Effects[i].TriggerSpell;
        if (!reqSpell)
            continue;

        Item* item = NULL;
        for (uint8 j = EQUIPMENT_SLOT_START; j < EQUIPMENT_SLOT_END; ++j)
        {
            item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, j);
            if (item && item->GetTemplate()->RequiredSpell == reqSpell)
            {
                //player has item equipped that require specialty. Not allow to unlearn, player has to unequip first
                TC_LOG_DEBUG("scripts", "player attempt to unlearn spell %u, but item %u is equipped.", reqSpell, item->GetEntry());
                return false;
            }
        }
    }
    return true;
}

void ProfessionUnlearnSpells(Player* player, uint32 type)
{
    switch (type)
    {
        case S_UNLEARN_WEAPON:                              // S_UNLEARN_WEAPON
            player->RemoveSpell(36125);                     // Light Earthforged Blade
            player->RemoveSpell(36128);                     // Light Emberforged Hammer
            player->RemoveSpell(36126);                     // Light Skyforged Axe
            break;
        case S_UNLEARN_ARMOR:                               // S_UNLEARN_ARMOR
            player->RemoveSpell(36122);                     // Earthforged Leggings
            player->RemoveSpell(36129);                     // Heavy Earthforged Breastplate
            player->RemoveSpell(36130);                     // Stormforged Hauberk
            player->RemoveSpell(34533);                     // Breastplate of Kings
            player->RemoveSpell(34529);                     // Nether Chain Shirt
            player->RemoveSpell(34534);                     // Bulwark of Kings
            player->RemoveSpell(36257);                     // Bulwark of the Ancient Kings
            player->RemoveSpell(36256);                     // Embrace of the Twisting Nether
            player->RemoveSpell(34530);                     // Twisting Nether Chain Shirt
            player->RemoveSpell(36124);                     // Windforged Leggings
            break;
        case S_UNLEARN_HAMMER:                              // S_UNLEARN_HAMMER
            player->RemoveSpell(36262);                     // Dragonstrike
            player->RemoveSpell(34546);                     // Dragonmaw
            player->RemoveSpell(34545);                     // Drakefist Hammer
            player->RemoveSpell(36136);                     // Lavaforged Warhammer
            player->RemoveSpell(34547);                     // Thunder
            player->RemoveSpell(34567);                     // Deep Thunder
            player->RemoveSpell(36263);                     // Stormherald
            player->RemoveSpell(36137);                     // Great Earthforged Hammer
            break;
        case S_UNLEARN_AXE:                                 // S_UNLEARN_AXE
            player->RemoveSpell(36260);                     // Wicked Edge of the Planes
            player->RemoveSpell(34562);                     // Black Planar Edge
            player->RemoveSpell(34541);                     // The Planar Edge
            player->RemoveSpell(36134);                     // Stormforged Axe
            player->RemoveSpell(36135);                     // Skyforged Great Axe
            player->RemoveSpell(36261);                     // Bloodmoon
            player->RemoveSpell(34543);                     // Lunar Crescent
            player->RemoveSpell(34544);                     // Mooncleaver
            break;
        case S_UNLEARN_SWORD:                               // S_UNLEARN_SWORD
            player->RemoveSpell(36258);                     // Blazefury
            player->RemoveSpell(34537);                     // Blazeguard
            player->RemoveSpell(34535);                     // Fireguard
            player->RemoveSpell(36131);                     // Windforged Rapier
            player->RemoveSpell(36133);                     // Stoneforged Claymore
            player->RemoveSpell(34538);                     // Lionheart Blade
            player->RemoveSpell(34540);                     // Lionheart Champion
            player->RemoveSpell(36259);                     // Lionheart Executioner
            break;
        case S_UNLEARN_DRAGON:                              // S_UNLEARN_DRAGON
            player->RemoveSpell(36076);                     // Dragonstrike Leggings
            player->RemoveSpell(36079);                     // Golden Dragonstrike Breastplate
            player->RemoveSpell(35576);                     // Ebon Netherscale Belt
            player->RemoveSpell(35577);                     // Ebon Netherscale Bracers
            player->RemoveSpell(35575);                     // Ebon Netherscale Breastplate
            player->RemoveSpell(35582);                     // Netherstrike Belt
            player->RemoveSpell(35584);                     // Netherstrike Bracers
            player->RemoveSpell(35580);                     // Netherstrike Breastplate
            break;
        case S_UNLEARN_ELEMENTAL:                           // S_UNLEARN_ELEMENTAL
            player->RemoveSpell(36074);                     // Blackstorm Leggings
            player->RemoveSpell(36077);                     // Primalstorm Breastplate
            player->RemoveSpell(35590);                     // Primalstrike Belt
            player->RemoveSpell(35591);                     // Primalstrike Bracers
            player->RemoveSpell(35589);                     // Primalstrike Vest
            break;
        case S_UNLEARN_TRIBAL:                              // S_UNLEARN_TRIBAL
            player->RemoveSpell(35585);                     // Windhawk Hauberk
            player->RemoveSpell(35587);                     // Windhawk Belt
            player->RemoveSpell(35588);                     // Windhawk Bracers
            player->RemoveSpell(36075);                     // Wildfeather Leggings
            player->RemoveSpell(36078);                     // Living Crystal Breastplate
            break;
        case S_UNLEARN_SPELLFIRE:                           // S_UNLEARN_SPELLFIRE
            player->RemoveSpell(26752);                     // Spellfire Belt
            player->RemoveSpell(26753);                     // Spellfire Gloves
            player->RemoveSpell(26754);                     // Spellfire Robe
            break;
        case S_UNLEARN_MOONCLOTH:                           // S_UNLEARN_MOONCLOTH
            player->RemoveSpell(26760);                     // Primal Mooncloth Belt
            player->RemoveSpell(26761);                     // Primal Mooncloth Shoulders
            player->RemoveSpell(26762);                     // Primal Mooncloth Robe
            break;
        case S_UNLEARN_SHADOWEAVE:                          // S_UNLEARN_SHADOWEAVE
            player->RemoveSpell(26756);                     // Frozen Shadoweave Shoulders
            player->RemoveSpell(26757);                     // Frozen Shadoweave Boots
            player->RemoveSpell(26758);                     // Frozen Shadoweave Robe
            break;
        case S_UNLEARN_GOBLIN:                                   // S_UNLEARN_GOBLIN
            player->RemoveSpell(30565);                     // Foreman's Enchanted Helmet
            player->RemoveSpell(30566);                     // Foreman's Reinforced Helmet
            player->RemoveSpell(30563);                     // Goblin Rocket Launcher
            player->RemoveSpell(56514);                     // Global Thermal Sapper Charge
            player->RemoveSpell(36954);                     // Dimensional Ripper - Area 52
            player->RemoveSpell(23486);                     // Dimensional Ripper - Everlook
            player->RemoveSpell(23078);                     // Goblin Jumper Cables XL
            player->RemoveSpell(72952);                     // Shatter Rounds
            break;
        case S_UNLEARN_GNOMISH:                               // S_UNLEARN_GNOMISH
            player->RemoveSpell(30575);                     // Gnomish Battle Goggles
            player->RemoveSpell(30574);                     // Gnomish Power Goggles
            player->RemoveSpell(56473);                     // Gnomish X-Ray Specs
            player->RemoveSpell(30569);                     // Gnomish Poultryizer
            player->RemoveSpell(30563);                     // Ultrasafe Transporter - Toshley's Station
            player->RemoveSpell(23489);                     // Ultrasafe Transporter - Gadgetzan
            player->RemoveSpell(23129);                     // World Enlarger
            player->RemoveSpell(23096);                     // Gnomish Alarm-o-Bot
            player->RemoveSpell(72953);                     // Iceblade Arrow
            break;
    }
}

void ProcessUnlearnAction(Player* player, Creature* creature, uint32 spellId, uint32 alternativeSpellId, int32 cost)
{
    if (EquippedOk(player, spellId))
    {
        if (player->HasEnoughMoney(cost))
        {
            player->CastSpell(player, spellId, true);
            ProfessionUnlearnSpells(player, spellId);
            player->ModifyMoney(-cost);
            if (alternativeSpellId)
                creature->CastSpell(player, alternativeSpellId, true);
        }
        else
            player->SendBuyError(BUY_ERR_NOT_ENOUGHT_MONEY, creature, 0, 0);
    }
    else
        player->SendEquipError(EQUIP_ERR_CANT_DO_RIGHT_NOW, NULL, NULL);
    player->CLOSE_GOSSIP_MENU();
}

/*###
# start menues alchemy
###*/

enum AlchemyQuest
{
    QUEST_TRANSMUTE = 10899,
    QUEST_ELIXIR    = 10902,
    QUEST_POTION    = 10897
};

std::vector<AlchemyQuest> alchemyQuests = { QUEST_TRANSMUTE, QUEST_ELIXIR, QUEST_POTION };

struct AlchemySpec
{
    AlchemyQuest quest;
    ProfessionSpells spell;
    ProfessionSpells learnSpell;
    ProfessionSpells unlearnSpell;
    const char* gossipLearn; ///@todo
    const char* gossipUnlearn;
};

std::map<uint32, AlchemySpec> alchemySpecTrainers =
{
    { 22427, { QUEST_TRANSMUTE, S_TRANSMUTE, S_LEARN_TRANSMUTE, S_UNLEARN_TRANSMUTE, // Zarevhi
        "Please teach me how to become a Master of Transmutations, Zarevhi", "I wish to unlearn Transmutation Mastery" } },
    { 19052, { QUEST_ELIXIR,    S_ELIXIR,    S_LEARN_ELIXIR,    S_UNLEARN_ELIXIR,    // Lorokeem
        "Please teach me how to become a Master of Elixirs, Lorokeem", "I wish to unlearn Elixir Mastery" } },
    { 17909, { QUEST_POTION,    S_POTION,    S_LEARN_POTION,    S_UNLEARN_POTION,    // Lauranna Thar'well
        "Please teach me how to become a Master of Potions, Lauranna", "I wish to unlearn Potion Mastery" } }
};

class npc_prof_alchemy : public CreatureScript
{
public:
    npc_prof_alchemy() : CreatureScript("npc_prof_alchemy") { }

    inline bool CanGossipSpecFromQuest(Player* player, AlchemyQuest quest)
    {
        bool questRewarded = false;
        for (AlchemyQuest q : alchemyQuests)
        {
            QuestStatus status = player->GetQuestStatus(q);
            if (status == QUEST_STATUS_REWARDED) // Any spec quest done
                questRewarded = true;
            if (status == QUEST_STATUS_COMPLETE || status == QUEST_STATUS_INCOMPLETE) // Other quest open
                return false;
        }
        return questRewarded && !(player->HasSpell(S_TRANSMUTE) || player->HasSpell(S_ELIXIR) || player->HasSpell(S_POTION)); // Any spec
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (creature->IsQuestGiver())
            player->PrepareQuestMenu(creature->GetGUID());

        if (creature->IsVendor())
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_VENDOR, player->GetOptionTextWithEntry(GOSSIP_TEXT_BROWSE_GOODS, GOSSIP_TEXT_BROWSE_GOODS_ID), GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);

        if (creature->IsTrainer())
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TRAINER, GOSSIP_TEXT_TRAIN, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRAIN);

        std::map<uint32, AlchemySpec>::const_iterator it = alchemySpecTrainers.find(creature->GetEntry());
        if (it != alchemySpecTrainers.end() && player->HasSkill(SKILL_ALCHEMY) && player->GetBaseSkillValue(SKILL_ALCHEMY) >= 350 && player->getLevel() > 67)
        {
            if (CanGossipSpecFromQuest(player, it->second.quest))
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, it->second.gossipLearn, GOSSIP_SENDER_LEARN, GOSSIP_ACTION_INFO_DEF + 1);
            else if (player->HasSpell(it->second.spell))
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, it->second.gossipUnlearn, GOSSIP_SENDER_UNLEARN, GOSSIP_ACTION_INFO_DEF + 2);
        }

        player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }

    void SendActionMenu(Player* player, Creature* creature, uint32 action)
    {
        std::map<uint32, AlchemySpec>::const_iterator it = alchemySpecTrainers.find(creature->GetEntry());
        switch (action)
        {
            case GOSSIP_ACTION_TRADE:
                player->GetSession()->SendListInventory(creature->GetGUID());
                break;
            case GOSSIP_ACTION_TRAIN:
                player->GetSession()->SendTrainerList(creature->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF + 1:
                if (it != alchemySpecTrainers.end())
                    ProcessCastaction(player, creature, it->second.spell, it->second.learnSpell, DoLearnCost(player));
                break;
            case GOSSIP_ACTION_INFO_DEF + 2:
                if (it != alchemySpecTrainers.end())
                    ProcessCastaction(player, creature, 0, it->second.unlearnSpell, DoLearnCost(player));
                break;
        }
    }

    void SendConfirmLearn(Player* player, Creature* creature, uint32 action)
    {
        if (!action)
            return;

        std::map<uint32, AlchemySpec>::const_iterator it = alchemySpecTrainers.find(creature->GetEntry());
        if (it != alchemySpecTrainers.end())
        {
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, it->second.gossipLearn, GOSSIP_SENDER_CHECK, action);
            player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
        }
    }

    void SendConfirmUnlearn(Player* player, Creature* creature, uint32 action)
    {
        if (!action)
            return;

        std::map<uint32, AlchemySpec>::const_iterator it = alchemySpecTrainers.find(creature->GetEntry());
        if (it != alchemySpecTrainers.end())
        {
            player->ADD_GOSSIP_ITEM_EXTENDED(0, it->second.gossipUnlearn, GOSSIP_SENDER_CHECK, action, BOX_UNLEARN_ALCHEMY_SPEC, DoHighUnlearnCost(player), false);
            player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
        }
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        switch (sender)
        {
            case GOSSIP_SENDER_MAIN:
                SendActionMenu(player, creature, action);
                break;

            case GOSSIP_SENDER_LEARN:
                SendConfirmLearn(player, creature, action);
                break;

            case GOSSIP_SENDER_UNLEARN:
                SendConfirmUnlearn(player, creature, action);
                break;

            case GOSSIP_SENDER_CHECK:
                SendActionMenu(player, creature, action);
                break;
        }
        return true;
    }
};

/*###
# start menues blacksmith
###*/

class npc_prof_blacksmith : public CreatureScript
{
public:
    npc_prof_blacksmith() : CreatureScript("npc_prof_blacksmith") { }

    inline bool HasWeaponSub(Player* player)
    {
        return (player->HasSpell(S_HAMMER) || player->HasSpell(S_AXE) || player->HasSpell(S_SWORD));
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (creature->IsQuestGiver())
            player->PrepareQuestMenu(creature->GetGUID());

        if (creature->IsVendor())
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_VENDOR, player->GetOptionTextWithEntry(GOSSIP_TEXT_BROWSE_GOODS, GOSSIP_TEXT_BROWSE_GOODS_ID), GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);

        if (creature->IsTrainer())
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TRAINER, GOSSIP_TEXT_TRAIN, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRAIN);

        uint32 creatureId = creature->GetEntry();
        //WEAPONSMITH & ARMORSMITH
        if (player->GetBaseSkillValue(SKILL_BLACKSMITHING) >= 225)
        {
            switch (creatureId)
            {
                case 11145:                                     //Myolor Sunderfury
                case 11176:                                     //Krathok Moltenfist
                    if (!player->HasSpell(S_ARMOR) && !player->HasSpell(S_WEAPON) && player->GetReputationRank(REP_ARMOR) >= REP_FRIENDLY)
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_ARMOR_LEARN,   GOSSIP_SENDER_MAIN,          GOSSIP_ACTION_INFO_DEF + 1);
                    if (!player->HasSpell(S_WEAPON) && !player->HasSpell(S_ARMOR) && player->GetReputationRank(REP_WEAPON) >= REP_FRIENDLY)
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_WEAPON_LEARN,  GOSSIP_SENDER_MAIN,          GOSSIP_ACTION_INFO_DEF + 2);
                    break;
                case 11146:                                     //Ironus Coldsteel
                case 11178:                                     //Borgosh Corebender
                    if (player->HasSpell(S_WEAPON))
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_WEAPON_UNLEARN,    GOSSIP_SENDER_UNLEARN,   GOSSIP_ACTION_INFO_DEF + 3);
                    break;
                case 5164:                                      //Grumnus Steelshaper
                case 11177:                                     //Okothos Ironrager
                    if (player->HasSpell(S_ARMOR))
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_ARMOR_UNLEARN,     GOSSIP_SENDER_UNLEARN,   GOSSIP_ACTION_INFO_DEF + 4);
                    break;
            }
        }
        //WEAPONSMITH SPEC
        if (player->HasSpell(S_WEAPON) && player->getLevel() > 49 && player->GetBaseSkillValue(SKILL_BLACKSMITHING) >= 250)
        {
            switch (creatureId)
            {
                case 11191:                                     //Lilith the Lithe
                    if (!HasWeaponSub(player))
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_HAMMER,       GOSSIP_SENDER_LEARN,    GOSSIP_ACTION_INFO_DEF + 5);
                    if (player->HasSpell(S_HAMMER))
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_UNLEARN_HAMMER,     GOSSIP_SENDER_UNLEARN,  GOSSIP_ACTION_INFO_DEF + 8);
                    break;
                case 11192:                                     //Kilram
                    if (!HasWeaponSub(player))
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_AXE,          GOSSIP_SENDER_LEARN,    GOSSIP_ACTION_INFO_DEF + 6);
                    if (player->HasSpell(S_AXE))
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_UNLEARN_AXE,        GOSSIP_SENDER_UNLEARN,  GOSSIP_ACTION_INFO_DEF + 9);
                    break;
                case 11193:                                     //Seril Scourgebane
                    if (!HasWeaponSub(player))
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_SWORD,        GOSSIP_SENDER_LEARN,    GOSSIP_ACTION_INFO_DEF + 7);
                    if (player->HasSpell(S_SWORD))
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_UNLEARN_SWORD,      GOSSIP_SENDER_UNLEARN,  GOSSIP_ACTION_INFO_DEF + 10);
                    break;
            }
        }

        player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }

    void SendActionMenu(Player* player, Creature* creature, uint32 action)
    {
        switch (action)
        {
            case GOSSIP_ACTION_TRADE:
                player->GetSession()->SendListInventory(creature->GetGUID());
                break;
            case GOSSIP_ACTION_TRAIN:
                player->GetSession()->SendTrainerList(creature->GetGUID());
                break;
                //Learn Armor/Weapon
            case GOSSIP_ACTION_INFO_DEF + 1:
                if (!player->HasSpell(S_ARMOR))
                {
                    player->CastSpell(player, S_LEARN_ARMOR, true);
                    //_Creature->CastSpell(player, S_REP_ARMOR, true);
                }
                player->CLOSE_GOSSIP_MENU();
                break;
            case GOSSIP_ACTION_INFO_DEF + 2:
                if (!player->HasSpell(S_WEAPON))
                {
                    player->CastSpell(player, S_LEARN_WEAPON, true);
                    //_Creature->CastSpell(player, S_REP_WEAPON, true);
                }
                player->CLOSE_GOSSIP_MENU();
                break;
                //Unlearn Armor/Weapon
            case GOSSIP_ACTION_INFO_DEF + 3:
                if (HasWeaponSub(player))
                {
                                                                //unknown textID (TALK_MUST_UNLEARN_WEAPON)
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
                }
                else
                    ProcessUnlearnAction(player, creature, S_UNLEARN_WEAPON, S_REP_ARMOR, DoLowUnlearnCost(player));
                break;
            case GOSSIP_ACTION_INFO_DEF + 4:
                ProcessUnlearnAction(player, creature, S_UNLEARN_ARMOR, S_REP_WEAPON, DoLowUnlearnCost(player));
                break;
                //Learn Hammer/Axe/Sword
            case GOSSIP_ACTION_INFO_DEF + 5:
                player->CastSpell(player, S_LEARN_HAMMER, true);
                player->CLOSE_GOSSIP_MENU();
                break;
            case GOSSIP_ACTION_INFO_DEF + 6:
                player->CastSpell(player, S_LEARN_AXE, true);
                player->CLOSE_GOSSIP_MENU();
                break;
            case GOSSIP_ACTION_INFO_DEF + 7:
                player->CastSpell(player, S_LEARN_SWORD, true);
                player->CLOSE_GOSSIP_MENU();
                break;
                //Unlearn Hammer/Axe/Sword
            case GOSSIP_ACTION_INFO_DEF + 8:
                ProcessUnlearnAction(player, creature, S_UNLEARN_HAMMER, 0, DoMedUnlearnCost(player));
                break;
            case GOSSIP_ACTION_INFO_DEF + 9:
                ProcessUnlearnAction(player, creature, S_UNLEARN_AXE, 0, DoMedUnlearnCost(player));
                break;
            case GOSSIP_ACTION_INFO_DEF + 10:
                ProcessUnlearnAction(player, creature, S_UNLEARN_SWORD, 0, DoMedUnlearnCost(player));
                break;
        }
    }

    void SendConfirmLearn(Player* player, Creature* creature, uint32 action)
    {
        if (action)
        {
            switch (creature->GetEntry())
            {
                case 11191:
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_HAMMER, GOSSIP_SENDER_CHECK, action);
                                                                //unknown textID (TALK_HAMMER_LEARN)
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
                    break;
                case 11192:
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_AXE,    GOSSIP_SENDER_CHECK, action);
                                                                //unknown textID (TALK_AXE_LEARN)
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
                    break;
                case 11193:
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_SWORD,  GOSSIP_SENDER_CHECK, action);
                                                                //unknown textID (TALK_SWORD_LEARN)
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
                    break;
            }
        }
    }

    void SendConfirmUnlearn(Player* player, Creature* creature, uint32 action)
    {
        if (action)
        {
            switch (creature->GetEntry())
            {
                case 11146:                                     //Ironus Coldsteel
                case 11178:                                     //Borgosh Corebender
                case 5164:                                      //Grumnus Steelshaper
                case 11177:                                     //Okothos Ironrager
                    player->ADD_GOSSIP_ITEM_EXTENDED(0, GOSSIP_UNLEARN_SMITH_SPEC, GOSSIP_SENDER_CHECK, action, BOX_UNLEARN_ARMORORWEAPON, DoLowUnlearnCost(player), false);
                                                                //unknown textID (TALK_UNLEARN_AXEORWEAPON)
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
                    break;

                case 11191:
                    player->ADD_GOSSIP_ITEM_EXTENDED(0, GOSSIP_UNLEARN_HAMMER, GOSSIP_SENDER_CHECK, action,    BOX_UNLEARN_WEAPON_SPEC, DoMedUnlearnCost(player), false);
                                                                //unknown textID (TALK_HAMMER_UNLEARN)
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
                    break;
                case 11192:
                    player->ADD_GOSSIP_ITEM_EXTENDED(0, GOSSIP_UNLEARN_AXE, GOSSIP_SENDER_CHECK, action,       BOX_UNLEARN_WEAPON_SPEC, DoMedUnlearnCost(player), false);
                                                                //unknown textID (TALK_AXE_UNLEARN)
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
                    break;
                case 11193:
                    player->ADD_GOSSIP_ITEM_EXTENDED(0, GOSSIP_UNLEARN_SWORD, GOSSIP_SENDER_CHECK, action,     BOX_UNLEARN_WEAPON_SPEC, DoMedUnlearnCost(player), false);
                                                                //unknown textID (TALK_SWORD_UNLEARN)
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
                    break;
            }
        }
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        switch (sender)
        {
            case GOSSIP_SENDER_MAIN:
                SendActionMenu(player, creature, action);
                break;

            case GOSSIP_SENDER_LEARN:
                SendConfirmLearn(player, creature, action);
                break;

            case GOSSIP_SENDER_UNLEARN:
                SendConfirmUnlearn(player, creature, action);
                break;

            case GOSSIP_SENDER_CHECK:
                SendActionMenu(player, creature, action);
                break;
        }
        return true;
    }
};

/*###
# engineering trinkets
###*/

enum EngineeringTrinkets
{
    NPC_ZAP                     = 14742,
    NPC_JHORDY                  = 14743,
    NPC_KABLAM                  = 21493,
    NPC_SMILES                  = 21494,

    SPELL_LEARN_TO_EVERLOOK     = 23490,
    SPELL_LEARN_TO_GADGET       = 23491,
    SPELL_LEARN_TO_AREA52       = 36956,
    SPELL_LEARN_TO_TOSHLEY      = 36957,

    SPELL_TO_EVERLOOK           = 23486,
    SPELL_TO_GADGET             = 23489,
    SPELL_TO_AREA52             = 36954,
    SPELL_TO_TOSHLEY            = 36955,
};

#define GOSSIP_ITEM_ZAP         "This Dimensional Imploder sounds dangerous! How can I make one?"
#define GOSSIP_ITEM_JHORDY      "I must build a beacon for this marvelous device!"
#define GOSSIP_ITEM_KABLAM      "[PH] Unknown"

class npc_engineering_tele_trinket : public CreatureScript
{
public:
    npc_engineering_tele_trinket() : CreatureScript("npc_engineering_tele_trinket") { }

    bool CanLearn(Player* player, uint32 textId, uint32 altTextId, uint32 skillValue, uint32 reqSpellId, uint32 spellId, uint32& npcTextId)
    {
        bool res = false;
        npcTextId = textId;
        if (player->GetBaseSkillValue(SKILL_ENGINEERING) >= skillValue && player->HasSpell(reqSpellId))
        {
            if (!player->HasSpell(spellId))
                res = true;
            else
                npcTextId = altTextId;
        }
        return res;
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        uint32 npcTextId = 0;
        std::string gossipItem;
        bool canLearn = false;

        if (player->HasSkill(SKILL_ENGINEERING))
        {
            switch (creature->GetEntry())
            {
                case NPC_ZAP:
                    canLearn = CanLearn(player, 6092, 0, 260, S_GOBLIN, SPELL_TO_EVERLOOK, npcTextId);
                    if (canLearn)
                        gossipItem = GOSSIP_ITEM_ZAP;
                    break;
                case NPC_JHORDY:
                    canLearn = CanLearn(player, 7251, 7252, 260, S_GNOMISH, SPELL_TO_GADGET, npcTextId);
                    if (canLearn)
                        gossipItem = GOSSIP_ITEM_JHORDY;
                    break;
                case NPC_KABLAM:
                    canLearn = CanLearn(player, 10365, 0, 350, S_GOBLIN, SPELL_TO_AREA52, npcTextId);
                    if (canLearn)
                        gossipItem = GOSSIP_ITEM_KABLAM;
                    break;
                case NPC_SMILES:
                    canLearn = CanLearn(player, 10363, 0, 350, S_GNOMISH, SPELL_TO_TOSHLEY, npcTextId);
                    if (canLearn)
                        gossipItem = GOSSIP_ITEM_KABLAM;
                    break;
            }
        }

        if (canLearn)
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, gossipItem, creature->GetEntry(), GOSSIP_ACTION_INFO_DEF + 1);

        player->SEND_GOSSIP_MENU(npcTextId ? npcTextId : player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        if (action == GOSSIP_ACTION_INFO_DEF + 1)
            player->CLOSE_GOSSIP_MENU();

        if (sender != creature->GetEntry())
            return true;

        switch (sender)
        {
            case NPC_ZAP:
                player->CastSpell(player, SPELL_LEARN_TO_EVERLOOK, false);
                break;
            case NPC_JHORDY:
                player->CastSpell(player, SPELL_LEARN_TO_GADGET, false);
                break;
            case NPC_KABLAM:
                player->CastSpell(player, SPELL_LEARN_TO_AREA52, false);
                break;
            case NPC_SMILES:
                player->CastSpell(player, SPELL_LEARN_TO_TOSHLEY, false);
                break;
        }

        return true;
    }
};

/*###
# start menues leatherworking
###*/

class npc_prof_leather : public CreatureScript
{
public:
    npc_prof_leather() : CreatureScript("npc_prof_leather") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (creature->IsQuestGiver())
            player->PrepareQuestMenu(creature->GetGUID());

        if (creature->IsVendor())
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_VENDOR, player->GetOptionTextWithEntry(GOSSIP_TEXT_BROWSE_GOODS, GOSSIP_TEXT_BROWSE_GOODS_ID), GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);

        if (creature->IsTrainer())
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TRAINER, GOSSIP_TEXT_TRAIN, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRAIN);

        if (player->HasSkill(SKILL_LEATHERWORKING) && player->GetBaseSkillValue(SKILL_LEATHERWORKING) >= 250 && player->getLevel() > 49)
        {
            switch (creature->GetEntry())
            {
                case 7866:                                      //Peter Galen
                case 7867:                                      //Thorkaf Dragoneye
                    if (player->HasSpell(S_DRAGON))
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_UNLEARN_DRAGON,      GOSSIP_SENDER_UNLEARN, GOSSIP_ACTION_INFO_DEF + 1);
                    break;
                case 7868:                                      //Sarah Tanner
                case 7869:                                      //Brumn Winterhoof
                    if (player->HasSpell(S_ELEMENTAL))
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_UNLEARN_ELEMENTAL,   GOSSIP_SENDER_UNLEARN, GOSSIP_ACTION_INFO_DEF + 2);
                    break;
                case 7870:                                      //Caryssia Moonhunter
                case 7871:                                      //Se'Jib
                    if (player->HasSpell(S_TRIBAL))
                        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_UNLEARN_TRIBAL,      GOSSIP_SENDER_UNLEARN, GOSSIP_ACTION_INFO_DEF + 3);
                    break;
            }
        }

        player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }

    void SendActionMenu(Player* player, Creature* creature, uint32 action)
    {
        switch (action)
        {
            case GOSSIP_ACTION_TRADE:
                player->GetSession()->SendListInventory(creature->GetGUID());
                break;
            case GOSSIP_ACTION_TRAIN:
                player->GetSession()->SendTrainerList(creature->GetGUID());
                break;
                //Unlearn Leather
            case GOSSIP_ACTION_INFO_DEF + 1:
                ProcessUnlearnAction(player, creature, S_UNLEARN_DRAGON, 0, DoMedUnlearnCost(player));
                break;
            case GOSSIP_ACTION_INFO_DEF + 2:
                ProcessUnlearnAction(player, creature, S_UNLEARN_ELEMENTAL, 0, DoMedUnlearnCost(player));
                break;
            case GOSSIP_ACTION_INFO_DEF + 3:
                ProcessUnlearnAction(player, creature, S_UNLEARN_TRIBAL, 0, DoMedUnlearnCost(player));
                break;
        }
    }

    void SendConfirmUnlearn(Player* player, Creature* creature, uint32 action)
    {
        if (action)
        {
            switch (creature->GetEntry())
            {
                case 7866:                                      //Peter Galen
                case 7867:                                      //Thorkaf Dragoneye
                    player->ADD_GOSSIP_ITEM_EXTENDED(0, GOSSIP_UNLEARN_DRAGON, GOSSIP_SENDER_CHECK, action,    BOX_UNLEARN_LEATHER_SPEC, DoMedUnlearnCost(player), false);
                                                                //unknown textID ()
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
                    break;
                case 7868:                                      //Sarah Tanner
                case 7869:                                      //Brumn Winterhoof
                    player->ADD_GOSSIP_ITEM_EXTENDED(0, GOSSIP_UNLEARN_ELEMENTAL, GOSSIP_SENDER_CHECK, action, BOX_UNLEARN_LEATHER_SPEC, DoMedUnlearnCost(player), false);
                                                                //unknown textID ()
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
                    break;
                case 7870:                                      //Caryssia Moonhunter
                case 7871:                                      //Se'Jib
                    player->ADD_GOSSIP_ITEM_EXTENDED(0, GOSSIP_UNLEARN_TRIBAL, GOSSIP_SENDER_CHECK, action,    BOX_UNLEARN_LEATHER_SPEC, DoMedUnlearnCost(player), false);
                                                                //unknown textID ()
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
                    break;
            }
        }
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        switch (sender)
        {
            case GOSSIP_SENDER_MAIN:
                SendActionMenu(player, creature, action);
                break;

            case GOSSIP_SENDER_UNLEARN:
                SendConfirmUnlearn(player, creature, action);
                break;

            case GOSSIP_SENDER_CHECK:
                SendActionMenu(player, creature, action);
                break;
        }
        return true;
    }
};

/*###
# start menues tailoring
###*/

enum TailorQuest
{
    QUEST_MOONCLOTH     = 10831,
    QUEST_SPELLFIRE     = 10832,
    QUEST_SHADOWEAVE    = 10833
};

std::vector<TailorQuest> tailorQuests = { QUEST_MOONCLOTH, QUEST_SPELLFIRE, QUEST_SHADOWEAVE };

struct TailorSpec
{
    TailorQuest quest;
    ProfessionSpells spell;
    ProfessionSpells learnSpell;
    ProfessionSpells unlearnSpell;
    const char* gossipLearn; ///@todo
    const char* gossipUnlearn;
};

std::map<uint32, TailorSpec> tailorSpecTrainers =
{
    { 22208, { QUEST_MOONCLOTH,  S_MOONCLOTH,  S_LEARN_MOONCLOTH,  S_UNLEARN_MOONCLOTH,  // Nasmara Moonsong
        "Please teach me how to become a Mooncloth tailor", "I wish to unlearn Mooncloth Tailoring" } },
    { 22213, { QUEST_SPELLFIRE,  S_SPELLFIRE,  S_LEARN_SPELLFIRE,  S_UNLEARN_SPELLFIRE,  // Gidge Spellweaver
        "Please teach me how to become a Spellcloth tailor", "I wish to unlearn Spellfire Tailoring" } },
    { 22212, { QUEST_SHADOWEAVE, S_SHADOWEAVE, S_LEARN_SHADOWEAVE, S_UNLEARN_SHADOWEAVE, // Andrion Darkspinner
        "Please teach me how to become a Shadoweave tailor", "I wish to unlearn Shadoweave Tailoring" } }
};

class npc_prof_tailor : public CreatureScript
{
public:
    npc_prof_tailor() : CreatureScript("npc_prof_tailor") { }

    inline bool CanGossipSpecFromQuest(Player* player, TailorQuest quest)
    {
        bool questRewarded = false;
        for (TailorQuest q : tailorQuests)
        {
            QuestStatus status = player->GetQuestStatus(q);
            if (status == QUEST_STATUS_REWARDED) // Any spec quest done
                questRewarded = true;
            if (status == QUEST_STATUS_COMPLETE || status == QUEST_STATUS_INCOMPLETE) // Other quest open
                return false;
        }
        return questRewarded && !(player->HasSpell(S_MOONCLOTH) || player->HasSpell(S_SPELLFIRE) || player->HasSpell(S_SHADOWEAVE)); // Any spec
    }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (creature->IsQuestGiver())
            player->PrepareQuestMenu(creature->GetGUID());

        if (creature->IsVendor())
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_VENDOR, player->GetOptionTextWithEntry(GOSSIP_TEXT_BROWSE_GOODS, GOSSIP_TEXT_BROWSE_GOODS_ID), GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);

        if (creature->IsTrainer())
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TRAINER, GOSSIP_TEXT_TRAIN, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRAIN);

        std::map<uint32, TailorSpec>::const_iterator it = tailorSpecTrainers.find(creature->GetEntry());
        if (it != tailorSpecTrainers.end() && player->HasSkill(SKILL_TAILORING) && player->GetBaseSkillValue(SKILL_TAILORING) >= 350 && player->getLevel() > 59)
        {
            if (CanGossipSpecFromQuest(player, it->second.quest))
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, it->second.gossipLearn, GOSSIP_SENDER_LEARN, GOSSIP_ACTION_INFO_DEF + 1);
            else if (player->HasSpell(it->second.spell))
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, it->second.gossipUnlearn, GOSSIP_SENDER_UNLEARN, GOSSIP_ACTION_INFO_DEF + 2);
        }

        player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }

    void SendActionMenu(Player* player, Creature* creature, uint32 action)
    {
        std::map<uint32, TailorSpec>::const_iterator it = tailorSpecTrainers.find(creature->GetEntry());
        switch (action)
        {
            case GOSSIP_ACTION_TRADE:
                player->GetSession()->SendListInventory(creature->GetGUID());
                break;
            case GOSSIP_ACTION_TRAIN:
                player->GetSession()->SendTrainerList(creature->GetGUID());
                break;
            case GOSSIP_ACTION_INFO_DEF + 1:
                if (it != tailorSpecTrainers.end())
                    ProcessCastaction(player, creature, it->second.spell, it->second.learnSpell, DoLearnCost(player));
                break;
            case GOSSIP_ACTION_INFO_DEF + 2:
                if (it != tailorSpecTrainers.end())
                    ProcessCastaction(player, creature, 0, it->second.unlearnSpell, DoLearnCost(player));
                break;
        }
    }

    void SendConfirmLearn(Player* player, Creature* creature, uint32 action)
    {
        if (!action)
            return;

        std::map<uint32, TailorSpec>::const_iterator it = tailorSpecTrainers.find(creature->GetEntry());
        if (it != tailorSpecTrainers.end())
        {
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, it->second.gossipLearn, GOSSIP_SENDER_CHECK, action);
            player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
        }
    }

    void SendConfirmUnlearn(Player* player, Creature* creature, uint32 action)
    {
        if (!action)
            return;

        std::map<uint32, TailorSpec>::const_iterator it = tailorSpecTrainers.find(creature->GetEntry());
        if (it != tailorSpecTrainers.end())
        {
            player->ADD_GOSSIP_ITEM_EXTENDED(0, it->second.gossipUnlearn, GOSSIP_SENDER_CHECK, action, BOX_UNLEARN_TAILOR_SPEC, DoHighUnlearnCost(player), false);
            player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
        }
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        player->PlayerTalkClass->ClearMenus();
        switch (sender)
        {
            case GOSSIP_SENDER_MAIN:
                SendActionMenu(player, creature, action);
                break;

            case GOSSIP_SENDER_LEARN:
                SendConfirmLearn(player, creature, action);
                break;

            case GOSSIP_SENDER_UNLEARN:
                SendConfirmUnlearn(player, creature, action);
                break;

            case GOSSIP_SENDER_CHECK:
                SendActionMenu(player, creature, action);
                break;
        }
        return true;
    }
};

/*###
# start menues for Book "Soothsaying for Dummies"
###*/

class go_soothsaying_for_dummies : public GameObjectScript
{
public:
    go_soothsaying_for_dummies() : GameObjectScript("go_soothsaying_for_dummies") { }

    inline bool HasLeatherSpell(Player* player)
    {
        return (player->HasSpell(S_ELEMENTAL) || player->HasSpell(S_DRAGON) || player->HasSpell(S_TRIBAL));
    }

    inline bool HasEngineerSpell(Player* player)
    {
        return (player->HasSpell(S_GNOMISH) || player->HasSpell(S_GOBLIN));
    }

    bool OnGossipHello(Player* player, GameObject* go)
    {
        //LEATHERWORKING SPEC
        if (player->HasSkill(SKILL_LEATHERWORKING) && player->GetBaseSkillValue(SKILL_LEATHERWORKING) >= 225 && player->getLevel() > 39)
        {
            if (player->GetQuestRewardStatus(QUEST_TRIBAL_TEATHERWORKING) || player->GetQuestRewardStatus(QUEST_ELEMENTAL_LEATHERWORKING) || player->GetQuestRewardStatus(QUEST_DRAGONSCALE_LEATHERWORKING))
            {
                if (!HasLeatherSpell(player))
                {
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_DRAGON, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 1);
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_ELEMENTAL, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 2);
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_TRIBAL, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 3);
                    player->SEND_GOSSIP_MENU(TALK_LEATHER_SPEC_LEARN, go->GetGUID());
                    return true;
                }
            }
        }

        //ENGINEERING SPEC
        if (player->HasSkill(SKILL_ENGINEERING) && player->GetBaseSkillValue(SKILL_ENGINEERING) >= 200 && player->getLevel() > 9)
        {
            if (player->GetQuestRewardStatus(QUEST_SHOW_YOUR_WORK_1) || player->GetQuestRewardStatus(QUEST_SHOW_YOUR_WORK_2) || player->GetQuestRewardStatus(QUEST_SHOW_YOUR_WORK_3))
            {
                if (!HasEngineerSpell(player))
                {
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_GNOMISH, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 4);
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_GOBLIN, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 5);
                    //unknown textID (TALK_ENGINEER_SPEC_LEARN)
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(go), go->GetGUID());
                    return true;
                }

                if (player->HasSpell(S_GNOMISH))
                    player->ADD_GOSSIP_ITEM_EXTENDED(GOSSIP_ICON_CHAT, GOSSIP_UNLEARN_GNOMISH, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 6, BOX_UNLEARN_GNOMISH_SPEC, DoHighUnlearnCost(player), false);
                if (player->HasSpell(S_GOBLIN))
                    player->ADD_GOSSIP_ITEM_EXTENDED(GOSSIP_ICON_CHAT, GOSSIP_UNLEARN_GOBLIN, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 7, BOX_UNLEARN_GNOMISH_SPEC, DoHighUnlearnCost(player), false);
            }
        }

        player->SEND_GOSSIP_MENU(player->GetGossipTextId(go), go->GetGUID());
        return true;
    }


    void SendActionMenu(Player* player, GameObject* go, uint32 uiAction)
    {
        switch (uiAction)
        {
            //Learn Leatherworking spec
            case GOSSIP_ACTION_INFO_DEF + 1:
                if (!player->HasSpell(S_DRAGON))
                    player->CastSpell(player, S_LEARN_DRAGON, true);
                player->CLOSE_GOSSIP_MENU();
                break;
            case GOSSIP_ACTION_INFO_DEF + 2:
                if (!player->HasSpell(S_ELEMENTAL))
                    player->CastSpell(player, S_LEARN_ELEMENTAL, true);
                player->CLOSE_GOSSIP_MENU();
                break;
            case GOSSIP_ACTION_INFO_DEF + 3:
                if (!player->HasSpell(S_TRIBAL))
                    player->CastSpell(player, S_LEARN_TRIBAL, true);
                player->CLOSE_GOSSIP_MENU();
                break;
                //Learn Engineering spec
            case GOSSIP_ACTION_INFO_DEF + 4:
                if (!player->HasSpell(S_GNOMISH))
                    player->CastSpell(player, S_LEARN_GNOMISH, true);
                player->CLOSE_GOSSIP_MENU();
                break;
            case GOSSIP_ACTION_INFO_DEF + 5:
                if (!player->HasSpell(S_GOBLIN))
                    player->CastSpell(player, S_LEARN_GOBLIN, true);
                player->CLOSE_GOSSIP_MENU();
                break;
                //Unlearn Engineering spec
            case GOSSIP_ACTION_INFO_DEF + 6:
                ProcessUnlearnAction(player, 0, S_UNLEARN_GNOMISH, 0, DoHighUnlearnCost(player));
                break;
            case GOSSIP_ACTION_INFO_DEF + 7:
                ProcessUnlearnAction(player, 0, S_UNLEARN_GOBLIN, 0, DoHighUnlearnCost(player));
                break;
        }
    }

    bool OnGossipSelect(Player* player, GameObject* go, uint32 uiSender, uint32 uiAction)
    {
        player->PlayerTalkClass->ClearMenus();

        switch (uiSender)
        {
            case GOSSIP_SENDER_MAIN: 
                SendActionMenu(player, go, uiAction); 
                break;
            case GOSSIP_SENDER_CHECK: 
                SendActionMenu(player, go, uiAction); 
                break;
        }
        return true;
    }
};

/*###
# start menues for NPC "Narain Soothfancy"
###*/

class npc_narain_soothfancy : public CreatureScript
{
public:
    npc_narain_soothfancy() : CreatureScript("npc_narain_soothfancy") { }

    inline bool HasLeatherSpell(Player* player)
    {
        return (player->HasSpell(S_ELEMENTAL) || player->HasSpell(S_DRAGON) || player->HasSpell(S_TRIBAL));
    }

    inline bool HasEngineerSpell(Player* player)
    {
        return (player->HasSpell(S_GNOMISH) || player->HasSpell(S_GOBLIN));
    }

    bool OnGossipHello(Player* player, Creature* creature)
    {
        if (creature->IsQuestGiver())
            player->PrepareQuestMenu(creature->GetGUID());

        //LEATHERWORKING SPEC
        if (player->HasSkill(SKILL_LEATHERWORKING) && player->GetBaseSkillValue(SKILL_LEATHERWORKING) >= 225 && player->getLevel() > 39)
        {
            if (player->GetQuestRewardStatus(QUEST_TRIBAL_TEATHERWORKING) || player->GetQuestRewardStatus(QUEST_ELEMENTAL_LEATHERWORKING) || player->GetQuestRewardStatus(QUEST_DRAGONSCALE_LEATHERWORKING))
            {
                if (!HasLeatherSpell(player))
                {
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_DRAGON, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 1);
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_ELEMENTAL, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 2);
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_TRIBAL, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 3);
                    player->SEND_GOSSIP_MENU(TALK_LEATHER_SPEC_LEARN, creature->GetGUID());
                    return true;
                }
            }
        }

        //ENGINEERING SPEC
        if (player->HasSkill(SKILL_ENGINEERING) && player->GetBaseSkillValue(SKILL_ENGINEERING) >= 200 && player->getLevel() > 9)
        {
            if (player->GetQuestRewardStatus(QUEST_SHOW_YOUR_WORK_1) || player->GetQuestRewardStatus(QUEST_SHOW_YOUR_WORK_2) || player->GetQuestRewardStatus(QUEST_SHOW_YOUR_WORK_3))
            {
                if (!HasEngineerSpell(player))
                {
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_GNOMISH, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 4);
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_LEARN_GOBLIN, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 5);
                    //unknown textID (TALK_ENGINEER_SPEC_LEARN)
                    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
                    return true;
                }

                if (player->HasSpell(S_GNOMISH))
                    player->ADD_GOSSIP_ITEM_EXTENDED(GOSSIP_ICON_CHAT, GOSSIP_UNLEARN_GNOMISH, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 6, BOX_UNLEARN_GNOMISH_SPEC, DoHighUnlearnCost(player), false);
                if (player->HasSpell(S_GOBLIN))
                    player->ADD_GOSSIP_ITEM_EXTENDED(GOSSIP_ICON_CHAT, GOSSIP_UNLEARN_GOBLIN, GOSSIP_SENDER_CHECK, GOSSIP_ACTION_INFO_DEF + 7, BOX_UNLEARN_GNOMISH_SPEC, DoHighUnlearnCost(player), false);
            }
        }

        player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetGUID());
        return true;
    }


    void SendActionMenu(Player* player, Creature* creature, uint32 uiAction)
    {
        switch (uiAction)
        {
            //Learn Leatherworking spec
            case GOSSIP_ACTION_INFO_DEF + 1:
                if (!player->HasSpell(S_DRAGON))
                    player->CastSpell(player, S_LEARN_DRAGON, true);
                player->CLOSE_GOSSIP_MENU();
                break;
            case GOSSIP_ACTION_INFO_DEF + 2:
                if (!player->HasSpell(S_ELEMENTAL))
                    player->CastSpell(player, S_LEARN_ELEMENTAL, true);
                player->CLOSE_GOSSIP_MENU();
                break;
            case GOSSIP_ACTION_INFO_DEF + 3:
                if (!player->HasSpell(S_TRIBAL))
                    player->CastSpell(player, S_LEARN_TRIBAL, true);
                player->CLOSE_GOSSIP_MENU();
                break;
                //Learn Engineering spec
            case GOSSIP_ACTION_INFO_DEF + 4:
                if (!player->HasSpell(S_GNOMISH))
                    player->CastSpell(player, S_LEARN_GNOMISH, true);
                player->CLOSE_GOSSIP_MENU();
                break;
            case GOSSIP_ACTION_INFO_DEF + 5:
                if (!player->HasSpell(S_GOBLIN))
                    player->CastSpell(player, S_LEARN_GOBLIN, true);
                player->CLOSE_GOSSIP_MENU();
                break;
                //Unlearn Engineering spec
            case GOSSIP_ACTION_INFO_DEF + 6:
                ProcessUnlearnAction(player, 0, S_UNLEARN_GNOMISH, 0, DoHighUnlearnCost(player));
                break;
            case GOSSIP_ACTION_INFO_DEF + 7:
                ProcessUnlearnAction(player, 0, S_UNLEARN_GOBLIN, 0, DoHighUnlearnCost(player));
                break;
        }
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 uiSender, uint32 uiAction)
    {
        player->PlayerTalkClass->ClearMenus();

        switch (uiSender)
        {
            case GOSSIP_SENDER_MAIN:
                SendActionMenu(player, creature, uiAction);
                break;
            case GOSSIP_SENDER_CHECK:
                SendActionMenu(player, creature, uiAction);
                break;
        }
        return true;
    }
};

void AddSC_npc_professions()
{
    new npc_prof_alchemy();
    new npc_prof_blacksmith();
    new npc_engineering_tele_trinket();
    new npc_prof_leather();
    new npc_prof_tailor();
    new go_soothsaying_for_dummies();
    new npc_narain_soothfancy();
}