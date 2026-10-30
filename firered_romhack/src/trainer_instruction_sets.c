#include "global.h"
#include "battle_setup.h"
#include "cereader_tool.h"
#include "field_specials.h"
#include "pokemon.h"
#include "text.h"
#include "trainer_instruction_sets.h"
#include "constants/easy_chat.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/opponents.h"
#include "constants/trainer_tower.h"
#include "constants/trainers.h"

#define DUMMY_TOWER_MON(iv) {.hpIV = iv, .attackIV = iv, .defenseIV = iv, .speedIV = iv, .spAttackIV = iv, .spDefenseIV = iv, .nickname = _("$$$$$$$$$$")}

enum
{
    TRAINER_DRILL_SWITCH_AND_SUPER_EFFECTIVE,
};

struct TrainerDrills
{
    u8 id;
    u8 challengeType;
    u8 objectiveTurns;
    u8 playerPartySize;
    u8 playerLeadIndex;
    u8 trainerPartySize;
    struct BattleTowerPokemon playerParty[PARTY_SIZE];
    struct TrainerTowerTrainer trainer;
};

const struct TrainerDrills gTrainerDrills[] = {
    {
        .id = TRAINER_DRILL_SWITCH_AND_SUPER_EFFECTIVE,
        .challengeType = CHALLENGE_TYPE_SINGLE,
        .objectiveTurns = 3,
        .playerPartySize = 2,
        .playerLeadIndex = 0,
        .trainerPartySize = 1,
        .playerParty = {
            {
                .species = SPECIES_TAUROS,
                .heldItem = ITEM_NONE,
                // Dig is stopped by Levitate; Normal moves cannot hit Ghost types.
                .moves = {MOVE_TAKE_DOWN, MOVE_TAIL_WHIP, MOVE_DIG, MOVE_SCARY_FACE},
                .level = 50,
                .hpIV = 15,
                .attackIV = 15,
                .defenseIV = 15,
                .speedIV = 15,
                .spAttackIV = 15,
                .spDefenseIV = 15,
                .abilityNum = 0,
                .nickname = _("TAUROS"),
                .friendship = 255,
            },
            {
                .species = SPECIES_HYPNO,
                .heldItem = ITEM_TWISTED_SPOON,
                .moves = {MOVE_SHADOW_BALL, MOVE_PSYCHIC, MOVE_SUBSTITUTE, MOVE_TOXIC},
                .level = 50,
                .spAttackIV = 15,
                .abilityNum = 0,
                .nickname = _("HYPNO"),
                .friendship = 255,
            },
        },
        .trainer = {
            .name = _("ROXANNE"),
            .facilityClass = FACILITY_CLASS_LEADER_ROXANNE,
            .textColor = 1,
            .speechBefore = {EC_WORD_I, EC_WORD_WILL, EC_WORD_NOT, EC_WORD_LOSE, EC_WORD_TO, EC_WORD_YOU},
            .speechWin = {EC_WORD_YOU, EC_WORD_ARE, EC_WORD_TOO, EC_WORD_WEAK, EC_WORD_TO, EC_WORD_WIN},
            .speechLose = {EC_WORD_I, EC_WORD_WILL, EC_WORD_TRAIN, EC_WORD_HARD, EC_WORD_NEXT, EC_WORD_TIME},
            .speechAfter = {EC_WORD_I, EC_WORD_WILL, EC_WORD_NOT, EC_WORD_LOSE, EC_WORD_TO, EC_WORD_YOU},
            .mons = {
                {
                    .species = SPECIES_GENGAR,
                    .heldItem = ITEM_NONE,
                    // Ghost moves cannot damage either Normal-type player Pokemon.
                    .moves = {MOVE_SHADOW_BALL, MOVE_BRICK_BREAK, MOVE_TAUNT, MOVE_NIGHT_SHADE},
                    .level = 50,
                    .otId = 0 | (4096 << 16),
                    .hpIV = 15,
                    .attackIV = 15,
                    .defenseIV = 15,
                    .speedIV = 15,
                    .spAttackIV = 15,
                    .spDefenseIV = 15,
                    .abilityNum = 0,
                    .nickname = _("GENGAR"),
                    .friendship = 255,
                },
                DUMMY_TOWER_MON(0),
                DUMMY_TOWER_MON(0),
                DUMMY_TOWER_MON(0),
                DUMMY_TOWER_MON(0),
                DUMMY_TOWER_MON(0),
            },
        },
    },
};

struct DrillPartyBackup
{
    bool8 active;
    u8 partyCount;
    struct Pokemon party[PARTY_SIZE];
};

static EWRAM_DATA struct DrillPartyBackup sDrillPartyBackup = {};

u16 BeginTrainerDrill(void)
{
    const struct TrainerDrills *drill = &gTrainerDrills[TRAINER_DRILL_SWITCH_AND_SUPER_EFFECTIVE];
    s32 i;

    if (sDrillPartyBackup.active
     || HasTrainerBeenFought(TRAINER_PALLET_TOWN_ROXANNE)
     || drill->playerPartySize == 0
     || drill->playerPartySize > PARTY_SIZE
     || drill->playerLeadIndex >= drill->playerPartySize)
        return FALSE;

    for (i = 0; i < drill->playerPartySize; i++)
    {
        if (drill->playerParty[i].species == SPECIES_NONE)
            return FALSE;
    }

    memcpy(sDrillPartyBackup.party, gPlayerParty, sizeof(gPlayerParty));
    sDrillPartyBackup.partyCount = gPlayerPartyCount;

    ZeroPlayerPartyMons();

    for (i = 0; i < drill->playerPartySize; i++)
    {
        u8 sourceIndex = (drill->playerLeadIndex + i) % drill->playerPartySize;
        struct BattleTowerPokemon mon = drill->playerParty[sourceIndex];

        // Use the player's trainer ID for the temporary Pokémon.
        mon.otId = GetPlayerTrainerId();
        CreateBattleTowerMon(&gPlayerParty[i], &mon);
    }

    gPlayerPartyCount = drill->playerPartySize;
    sDrillPartyBackup.active = TRUE;
    return TRUE;
}

void RestoreTrainerDrillParty(void)
{
    if (!sDrillPartyBackup.active)
        return;

    memcpy(gPlayerParty, sDrillPartyBackup.party, sizeof(gPlayerParty));
    gPlayerPartyCount = sDrillPartyBackup.partyCount;
    sDrillPartyBackup.active = FALSE;
}
