#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_controllers.h"
#include "event_data.h"
#include "random.h"
#include "util.h"
#include "constants/abilities.h"
#include "constants/battle_move_effects.h"
#include "constants/item_effects.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/pokemon.h"
#include "constants/trainers.h"

static bool8 HasSuperEffectiveMoveAgainstOpponents(bool8 noRng);
static bool8 FindMonWithFlagsAndSuperEffective(u8 flags, u8 moduloPercent);
static bool8 HasEffectiveDamagingMove(struct BattlePokemon *attacker, u8 attackerId, struct BattlePokemon *defender, u8 defenderId);
static bool8 ShouldSwitchForBadMatchup(void);
static bool8 ShouldSwitchToAvoidSlowerKO(void);
static bool8 HasUsefulStatusOrSetupMove(void);
static void LoadSwitchCandidate(struct BattlePokemon *mon, u8 partyId);
static void LoadRevealedPlayerReserve(struct BattlePokemon *mon, u8 partyId);
static void LoadKnownPlayerPokemon(struct BattlePokemon *mon, u8 battlerId);
static void RememberPlayerBattleKnowledge(u8 battlerId);
static u32 GetKnownPlayerThreat(u8 battlerId, struct BattlePokemon *defender);
static u32 GetKnownPlayerPriorityThreat(u8 battlerId, struct BattlePokemon *defender, s8 *priority);
static u32 GetKnownPlayerThreatForReserve(u8 partyId, struct BattlePokemon *attacker, u8 defenderId);
static bool8 ShouldUseItem(void);
static u32 GetExpectedEndOfTurnDamage(struct BattlePokemon *mon, u8 battlerId, bool8 isActive);
static bool8 IsActiveMonSafeAgainstKnownThreats(void);
static bool8 ApplySwitchInHazards(struct BattlePokemon *mon);
static bool8 ShouldStayInAgainstFasterSwitchFallback(u8 partyId);
static void ApplySwitchInIntimidate(struct BattlePokemon *switchIn, struct BattlePokemon *opponent, u8 opponentId);
static u32 ApplySwitchInAbilityEffects(struct BattlePokemon *switchIn, u8 outgoingBattler, u8 *opposingBattlers, u8 numOpponents, u8 *outgoingAbility);
static u32 GetKnownPlayerThreatWithMon(u8 battlerId, struct BattlePokemon *defender, struct BattlePokemon *attacker);
static u32 GetKnownPlayerPriorityThreatWithMon(u8 battlerId, struct BattlePokemon *defender, s8 *priority, struct BattlePokemon *attacker);
static u8 GetKnownPlayerAbility(struct PlayerPartyKnowledge *knowledge, u16 species);

struct SwitchSpeedRange
{
    u32 min;
    u32 max;
};

static bool8 ShouldSwitchIfPerishSong(void)
{
    if (gStatuses3[gActiveBattler] & STATUS3_PERISH_SONG
     && gDisableStructs[gActiveBattler].perishSongTimer == 0)
    {
        *(gBattleStruct->AI_monToSwitchIntoId + (GetBattlerPosition(gActiveBattler) >> 1)) = PARTY_SIZE;
        BtlController_EmitTwoReturnValues(1, B_ACTION_SWITCH, 0);
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

static bool8 ShouldSwitchIfWonderGuard(void)
{
    u8 opposingBattler;
    u8 moveFlags;
    s32 i, j;
    u16 move;

    if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
        return FALSE;
    if (gBattleMons[GetBattlerAtPosition(B_POSITION_PLAYER_LEFT)].ability == ABILITY_WONDER_GUARD)
    {
        // Check if Pokemon has a super effective move.
        for (opposingBattler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT), i = 0; i < MAX_MON_MOVES; ++i)
        {
            move = gBattleMons[gActiveBattler].moves[i];
            if (move == MOVE_NONE)
                continue;
            moveFlags = AI_TypeCalc(move, gBattleMons[opposingBattler].species, gBattleMons[opposingBattler].ability);
            if (moveFlags & MOVE_RESULT_SUPER_EFFECTIVE)
                return FALSE;
        }
        // Find a Pokemon in the party that has a super effective move.
        for (i = 0; i < PARTY_SIZE; ++i)
        {
            if (GetMonData(&gEnemyParty[i], MON_DATA_HP) == 0
             || GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_NONE
             || GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_EGG
             || i == gBattlerPartyIndexes[gActiveBattler])
                continue;
            GetMonData(&gEnemyParty[i], MON_DATA_SPECIES); // Unused return value.
            GetMonData(&gEnemyParty[i], MON_DATA_ABILITY_NUM); // Unused return value.        
            for (opposingBattler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT), j = 0; j < MAX_MON_MOVES; ++j)
            {
                move = GetMonData(&gEnemyParty[i], MON_DATA_MOVE1 + j);
                if (move == MOVE_NONE)
                    continue;
                moveFlags = AI_TypeCalc(move, gBattleMons[opposingBattler].species, gBattleMons[opposingBattler].ability);
                if (moveFlags & MOVE_RESULT_SUPER_EFFECTIVE && Random() % 3 < 2)
                {
                    // We found a mon.
                    *(gBattleStruct->AI_monToSwitchIntoId + (GetBattlerPosition(gActiveBattler) >> 1)) = i;
                    BtlController_EmitTwoReturnValues(1, B_ACTION_SWITCH, 0);
                    return TRUE;
                }
            }
        }
    }
    return FALSE; // There is not a single Pokemon in the party that has a super effective move against a mon with Wonder Guard.
}

static bool8 FindMonThatAbsorbsOpponentsMove(void)
{
    u8 battlerIn1, battlerIn2;
    u8 absorbingTypeAbility;
    s32 i;

    if ((HasSuperEffectiveMoveAgainstOpponents(TRUE) && Random() % 3) 
    || (gLastLandedMoves[gActiveBattler] == MOVE_NONE))
        return FALSE;
    if (gLastLandedMoves[gActiveBattler] == 0xFFFF
     || gBattleMoves[gLastLandedMoves[gActiveBattler]].power == 0)
        return FALSE;
    if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
    {
        battlerIn1 = gActiveBattler;
        if (gAbsentBattlerFlags & gBitTable[GetBattlerAtPosition(BATTLE_PARTNER(GetBattlerPosition(gActiveBattler)))])
            battlerIn2 = gActiveBattler;
        else
            battlerIn2 = GetBattlerAtPosition(BATTLE_PARTNER(GetBattlerPosition(gActiveBattler)));
    }
    else
    {
        battlerIn1 = gActiveBattler;
        battlerIn2 = gActiveBattler;
    }
    if (gBattleMoves[gLastLandedMoves[gActiveBattler]].type == TYPE_FIRE)
        absorbingTypeAbility = ABILITY_FLASH_FIRE;
    else if (gBattleMoves[gLastLandedMoves[gActiveBattler]].type == TYPE_WATER)
        absorbingTypeAbility = ABILITY_WATER_ABSORB;
    else if (gBattleMoves[gLastLandedMoves[gActiveBattler]].type == TYPE_ELECTRIC)
        absorbingTypeAbility = ABILITY_VOLT_ABSORB;
    else
        return FALSE;
    if (gBattleMons[gActiveBattler].ability == absorbingTypeAbility)
        return FALSE;
    for (i = 0; i < PARTY_SIZE; ++i)
    {
        u16 species;
        u8 monAbility;

        if ((GetMonData(&gEnemyParty[i], MON_DATA_HP) == 0)
         || (GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_NONE)
         || (GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_EGG)
         || (i == gBattlerPartyIndexes[battlerIn1])
         || (i == gBattlerPartyIndexes[battlerIn2])
         || (i == *(gBattleStruct->monToSwitchIntoId + battlerIn1))
         || (i == *(gBattleStruct->monToSwitchIntoId + battlerIn2)))
            continue;
        species = GetMonData(&gEnemyParty[i], MON_DATA_SPECIES);
        if (GetMonData(&gEnemyParty[i], MON_DATA_ABILITY_NUM) != ABILITY_NONE)
            monAbility = gSpeciesInfo[species].abilities[1];
        else
            monAbility = gSpeciesInfo[species].abilities[0];
        if (absorbingTypeAbility == monAbility && Random() & 1)
        {
            // we found a mon
            *(gBattleStruct->AI_monToSwitchIntoId + (GetBattlerPosition(gActiveBattler) >> 1)) = i;
            BtlController_EmitTwoReturnValues(1, B_ACTION_SWITCH, 0);
            return TRUE;
        }
    }
    return FALSE;
}

static bool8 ShouldSwitchIfNaturalCure(void)
{
    u32 status = gBattleMons[gActiveBattler].status1;
    bool8 damagingStatus = status & (STATUS1_POISON | STATUS1_TOXIC_POISON | STATUS1_BURN);

    if (!(status & STATUS1_ANY)
     || gBattleMons[gActiveBattler].ability != ABILITY_NATURAL_CURE
     || (!damagingStatus && gBattleMons[gActiveBattler].hp < gBattleMons[gActiveBattler].maxHP / 2))
        return FALSE;
    if ((gLastLandedMoves[gActiveBattler] == MOVE_NONE || gLastLandedMoves[gActiveBattler] == 0xFFFF)
     && (Random() & 1))
    {
        *(gBattleStruct->AI_monToSwitchIntoId + (GetBattlerPosition(gActiveBattler) >> 1)) = PARTY_SIZE;
        BtlController_EmitTwoReturnValues(1, B_ACTION_SWITCH, 0);
        return TRUE;
    }
    else if (gLastLandedMoves[gActiveBattler] < MOVES_COUNT
          && gBattleMoves[gLastLandedMoves[gActiveBattler]].power == 0
          && (Random() & 1))
    {
        *(gBattleStruct->AI_monToSwitchIntoId + (GetBattlerPosition(gActiveBattler) >> 1)) = PARTY_SIZE;
        BtlController_EmitTwoReturnValues(1, B_ACTION_SWITCH, 0);
        return TRUE;
    }
    if (FindMonWithFlagsAndSuperEffective(MOVE_RESULT_DOESNT_AFFECT_FOE, 1)
     || FindMonWithFlagsAndSuperEffective(MOVE_RESULT_NOT_VERY_EFFECTIVE, 1))
        return TRUE;
    if (Random() & 1)
    {
        *(gBattleStruct->AI_monToSwitchIntoId + (GetBattlerPosition(gActiveBattler) >> 1)) = PARTY_SIZE;
        BtlController_EmitTwoReturnValues(1, B_ACTION_SWITCH, 0);
        return TRUE;
    }
    return FALSE;
}

static bool8 HasSuperEffectiveMoveAgainstOpponents(bool8 noRng)
{
    struct BattlePokemon opponentMon;
    u8 opposingBattler;
    s32 i;
    u8 moveFlags;
    u16 move;

    opposingBattler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
    if (!(gAbsentBattlerFlags & gBitTable[opposingBattler]))
    {
        LoadKnownPlayerPokemon(&opponentMon, opposingBattler);
        for (i = 0; i < MAX_MON_MOVES; ++i)
        {
            move = gBattleMons[gActiveBattler].moves[i];
            if (move == MOVE_NONE || gBattleMons[gActiveBattler].pp[i] == 0
             || gBattleMoves[move].power == 0
             || gDisableStructs[gActiveBattler].disabledMove == move
             || (gDisableStructs[gActiveBattler].encoredMove != MOVE_NONE
              && gDisableStructs[gActiveBattler].encoredMove != move))
                continue;

            moveFlags = AI_TypeCalc(move, opponentMon.species, opponentMon.ability);
            if (moveFlags & MOVE_RESULT_SUPER_EFFECTIVE)
            {
                if (noRng || (Random() % 10))
                    return TRUE;
            }
        }
    }
    if (!(gBattleTypeFlags & BATTLE_TYPE_DOUBLE))
        return FALSE;
    opposingBattler = GetBattlerAtPosition(BATTLE_PARTNER(B_POSITION_PLAYER_LEFT));
    if (!(gAbsentBattlerFlags & gBitTable[opposingBattler]))
    {
        LoadKnownPlayerPokemon(&opponentMon, opposingBattler);
        for (i = 0; i < MAX_MON_MOVES; ++i)
        {
            move = gBattleMons[gActiveBattler].moves[i];
            if (move == MOVE_NONE || gBattleMons[gActiveBattler].pp[i] == 0
             || gBattleMoves[move].power == 0
             || gDisableStructs[gActiveBattler].disabledMove == move
             || (gDisableStructs[gActiveBattler].encoredMove != MOVE_NONE
              && gDisableStructs[gActiveBattler].encoredMove != move))
                continue;
            moveFlags = AI_TypeCalc(move, opponentMon.species, opponentMon.ability);
            if (moveFlags & MOVE_RESULT_SUPER_EFFECTIVE)
            {
                if (noRng)
                    return TRUE;
                if (Random() % 10 != 0)
                    return TRUE;
            }
        }
    }
    return FALSE;
}

static bool8 AreStatsRaised(void)
{
    u8 buffedStatsValue = 0;
    s32 i;

    for (i = 0; i < NUM_BATTLE_STATS; ++i)
    {
        if (gBattleMons[gActiveBattler].statStages[i] > 6)
            buffedStatsValue += gBattleMons[gActiveBattler].statStages[i] - 6;
    }
    return (buffedStatsValue > 3);
}

static bool8 FindMonWithFlagsAndSuperEffective(u8 flags, u8 moduloPercent)
{
    u8 battlerIn1, battlerIn2;
    s32 i, j;
    u16 move;
    u8 moveFlags;

    if (gLastLandedMoves[gActiveBattler] == 0)
        return FALSE;
    if ((gLastLandedMoves[gActiveBattler] == 0xFFFF)
     || (gLastHitBy[gActiveBattler] == 0xFF)
     || (gBattleMoves[gLastLandedMoves[gActiveBattler]].power == 0))
        return FALSE;
    if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
    {
        battlerIn1 = gActiveBattler;
        if (gAbsentBattlerFlags & gBitTable[GetBattlerAtPosition(BATTLE_PARTNER(GetBattlerPosition(gActiveBattler)))])
            battlerIn2 = gActiveBattler;
        else
            battlerIn2 = GetBattlerAtPosition(BATTLE_PARTNER(GetBattlerPosition(gActiveBattler)));
    }
    else
    {
        battlerIn1 = gActiveBattler;
        battlerIn2 = gActiveBattler;
    }
    for (i = 0; i < PARTY_SIZE; ++i)
    {
        u16 species;
        u8 monAbility;

        if ((GetMonData(&gEnemyParty[i], MON_DATA_HP) == 0)
         || (GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_NONE)
         || (GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_EGG)
         || (i == gBattlerPartyIndexes[battlerIn1])
         || (i == gBattlerPartyIndexes[battlerIn2])
         || (i == *(gBattleStruct->monToSwitchIntoId + battlerIn1))
         || (i == *(gBattleStruct->monToSwitchIntoId + battlerIn2)))
            continue;
        species = GetMonData(&gEnemyParty[i], MON_DATA_SPECIES);
        if (GetMonData(&gEnemyParty[i], MON_DATA_ABILITY_NUM) != ABILITY_NONE)
            monAbility = gSpeciesInfo[species].abilities[1];
        else
            monAbility = gSpeciesInfo[species].abilities[0];
        moveFlags = AI_TypeCalc(gLastLandedMoves[gActiveBattler], species, monAbility);
        if (moveFlags & flags)
        {
            battlerIn1 = gLastHitBy[gActiveBattler];
            for (j = 0; j < MAX_MON_MOVES; ++j)
            {
                move = GetMonData(&gEnemyParty[i], MON_DATA_MOVE1 + j);
                if (move == MOVE_NONE)
                    continue;
                moveFlags = AI_TypeCalc(move, gBattleMons[battlerIn1].species, gBattleMons[battlerIn1].ability);
                if (moveFlags & MOVE_RESULT_SUPER_EFFECTIVE && Random() % moduloPercent == 0)
                {
                    *(gBattleStruct->AI_monToSwitchIntoId + (GetBattlerPosition(gActiveBattler) >> 1)) = i;
                    BtlController_EmitTwoReturnValues(1, B_ACTION_SWITCH, 0);
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

static bool8 ShouldSwitch(void)
{
    u8 battlerIn1, battlerIn2;
    s32 i;
    s32 availableToSwitch;

    if ((gBattleMons[gActiveBattler].status2 & (STATUS2_WRAPPED | STATUS2_ESCAPE_PREVENTION))
     || (gStatuses3[gActiveBattler] & STATUS3_ROOTED)
     || AbilityBattleEffects(ABILITYEFFECT_CHECK_OTHER_SIDE, gActiveBattler, ABILITY_SHADOW_TAG, 0, 0)
     || AbilityBattleEffects(ABILITYEFFECT_CHECK_OTHER_SIDE, gActiveBattler, ABILITY_ARENA_TRAP, 0, 0))
        return FALSE; // misses the flying or levitate check
    if (AbilityBattleEffects(ABILITYEFFECT_FIELD_SPORT, 0, ABILITY_MAGNET_PULL, 0, 0))
        if ((gBattleMons[gActiveBattler].type1 == TYPE_STEEL) || (gBattleMons[gActiveBattler].type2 == TYPE_STEEL))
            return FALSE;
    availableToSwitch = 0;
    if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
    {
        battlerIn1 = gActiveBattler;
        if (gAbsentBattlerFlags & gBitTable[GetBattlerAtPosition(GetBattlerPosition(gActiveBattler) ^ BIT_FLANK)])
            battlerIn2 = gActiveBattler;
        else
            battlerIn2 = GetBattlerAtPosition(GetBattlerPosition(gActiveBattler) ^ BIT_FLANK);
    }
    else
    {
        battlerIn2 = gActiveBattler;
        battlerIn1 = gActiveBattler;
    }
    for (i = 0; i < PARTY_SIZE; ++i)
    {
        if ((GetMonData(&gEnemyParty[i], MON_DATA_HP) == 0)
         || (GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_NONE)
         || (GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG) == SPECIES_EGG)
         || (i == gBattlerPartyIndexes[battlerIn1])
         || (i == gBattlerPartyIndexes[battlerIn2])
         || (i == *(gBattleStruct->monToSwitchIntoId + battlerIn1))
         || (i == *(gBattleStruct->monToSwitchIntoId + battlerIn2)))
            continue;
        ++availableToSwitch;
    }
    if (!availableToSwitch)
        return FALSE;
    if (ShouldSwitchIfPerishSong()
     || ShouldSwitchIfWonderGuard()
     || FindMonThatAbsorbsOpponentsMove()
     || ShouldSwitchIfNaturalCure())
        return TRUE;
    if (ShouldSwitchToAvoidSlowerKO())
        return TRUE;
    if (AreStatsRaised())
        return FALSE;
    if (ShouldSwitchForBadMatchup())
        return TRUE;
    if (IsActiveMonSafeAgainstKnownThreats())
        return FALSE;
    if (FindMonWithFlagsAndSuperEffective(MOVE_RESULT_DOESNT_AFFECT_FOE, 2)
     || FindMonWithFlagsAndSuperEffective(MOVE_RESULT_NOT_VERY_EFFECTIVE, 3))
        return TRUE;
    return FALSE;
}

void AI_TrySwitchOrUseItem(void)
{
    u8 battlerIn1, battlerIn2;

    if (gBattleTypeFlags & BATTLE_TYPE_TRAINER)
    {
        if (ShouldSwitch())
        {
            if (*(gBattleStruct->AI_monToSwitchIntoId + (GetBattlerPosition(gActiveBattler) >> 1)) == 6)
            {
                s32 monToSwitchId = GetMostSuitableMonToSwitchInto();
                if (monToSwitchId == 6)
                {
                    if (!(gBattleTypeFlags & BATTLE_TYPE_DOUBLE))
                    {
                        battlerIn1 = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);
                        battlerIn2 = battlerIn1;
                    }
                    else
                    {
                        battlerIn1 = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);
                        battlerIn2 = GetBattlerAtPosition(B_POSITION_OPPONENT_RIGHT);
                    }
                    for (monToSwitchId = 0; monToSwitchId < PARTY_SIZE; ++monToSwitchId)
                    {
                        if ((!GetMonData(&gEnemyParty[monToSwitchId], MON_DATA_HP) == 0)
                         && (monToSwitchId != gBattlerPartyIndexes[battlerIn1])
                         && (monToSwitchId != gBattlerPartyIndexes[battlerIn2])
                         && (monToSwitchId != *(gBattleStruct->monToSwitchIntoId + battlerIn1))
                         && (monToSwitchId != *(gBattleStruct->monToSwitchIntoId + battlerIn2)))
                            break;
                    }
                    if (monToSwitchId < PARTY_SIZE && ShouldStayInAgainstFasterSwitchFallback(monToSwitchId))
                    {
                        BtlController_EmitTwoReturnValues(1, B_ACTION_USE_MOVE, (gActiveBattler ^ BIT_SIDE) << 8);
                        return;
                    }
                }
                *(gBattleStruct->AI_monToSwitchIntoId + (GetBattlerPosition(gActiveBattler) >> 1)) = monToSwitchId;
            }
            *(gBattleStruct->monToSwitchIntoId + gActiveBattler) = *(gBattleStruct->AI_monToSwitchIntoId + (GetBattlerPosition(gActiveBattler) >> 1));
            return;
        }
        else if (ShouldUseItem())
        {
            return;
        }
    }
    BtlController_EmitTwoReturnValues(1, B_ACTION_USE_MOVE, (gActiveBattler ^ BIT_SIDE) << 8);
}

static void ModulateByTypeEffectiveness(u8 atkType, u8 defType1, u8 defType2, u8 *var)
{
    s32 i = 0;

    while (TYPE_EFFECT_ATK_TYPE(i) != TYPE_ENDTABLE)
    {
        if (TYPE_EFFECT_ATK_TYPE(i) == TYPE_FORESIGHT)
        {
            i += 3;
            continue;
        }
        else if (TYPE_EFFECT_ATK_TYPE(i) == atkType)
        {
            // Check type1.
            if (TYPE_EFFECT_DEF_TYPE(i) == defType1)
                *var = (*var * TYPE_EFFECT_MULTIPLIER(i)) / 10;
            // Check type2.
            if (TYPE_EFFECT_DEF_TYPE(i) == defType2 && defType1 != defType2)
                *var = (*var * TYPE_EFFECT_MULTIPLIER(i)) / 10;
        }
        i += 3;
    }
}

static u32 GetSwitchMoveDamage(struct BattlePokemon *attacker, struct BattlePokemon *defender, u16 move, u16 powerOverride, u8 attackerId, u8 defenderId)
{
    u16 currentMove = gCurrentMove;
    u16 currentMovePower = gBattleMovePower;
    u16 currentDynamicBasePower = gDynamicBasePower;
    u8 currentCritMultiplier = gCritMultiplier;
    u8 currentBattlerAttacker = gBattlerAttacker;
    u8 currentBattlerTarget = gBattlerTarget;
    u8 currentDynamicMoveType = gBattleStruct->dynamicMoveType;
    u8 currentDamageMultiplier = gBattleScripting.dmgMultiplier;
    u32 currentMoveResultFlags = gMoveResultFlags;
    u8 typeMultiplier = 10;
    s32 damage;

    if (move == MOVE_NONE || move >= MOVES_COUNT || gBattleMoves[move].power == 0)
        return 0;

    gCurrentMove = move;
    gDynamicBasePower = 0;
    gBattleStruct->dynamicMoveType = 0;
    gBattleScripting.dmgMultiplier = 1;
    gMoveResultFlags = 0;
    gCritMultiplier = 1;
    gBattlerAttacker = attackerId;
    gBattlerTarget = defenderId;
    damage = CalculateBaseDamage(attacker, defender, move, gSideStatuses[GET_BATTLER_SIDE(defenderId)],
                                 powerOverride, 0, attackerId, defenderId);

    if (AI_TypeCalc(move, defender->species, defender->ability) & (MOVE_RESULT_MISSED | MOVE_RESULT_DOESNT_AFFECT_FOE))
    {
        damage = 0;
    }
    else
    {
        if (attacker->type1 == gBattleMoves[move].type || attacker->type2 == gBattleMoves[move].type)
            damage = damage * 15 / 10;
        ModulateByTypeEffectiveness(gBattleMoves[move].type, defender->type1, defender->type2, &typeMultiplier);
        damage = damage * typeMultiplier / 10;
        if (powerOverride == 0 && gBattleMoves[move].accuracy != 0)
            damage = damage * gBattleMoves[move].accuracy / 100;
    }

    gCurrentMove = currentMove;
    gBattleMovePower = currentMovePower;
    gDynamicBasePower = currentDynamicBasePower;
    gCritMultiplier = currentCritMultiplier;
    gBattlerAttacker = currentBattlerAttacker;
    gBattlerTarget = currentBattlerTarget;
    gBattleStruct->dynamicMoveType = currentDynamicMoveType;
    gBattleScripting.dmgMultiplier = currentDamageMultiplier;
    gMoveResultFlags = currentMoveResultFlags;

    return damage > 0 ? damage : 0;
}

static u32 GetExpectedEndOfTurnDamage(struct BattlePokemon *mon, u8 battlerId, bool8 isActive)
{
    u32 damage = 0;
    u32 healing = 0;
    u32 residualDamage;
    u32 status = mon->status1;

    if (mon->item == ITEM_LUM_BERRY)
        status = 0;
    else if (mon->item == ITEM_PECHA_BERRY)
        status &= ~(STATUS1_POISON | STATUS1_TOXIC_POISON | STATUS1_TOXIC_COUNTER);
    else if (mon->item == ITEM_RAWST_BERRY)
        status &= ~STATUS1_BURN;

    if (status & STATUS1_TOXIC_POISON)
    {
        u32 toxicTurn = (status & STATUS1_TOXIC_COUNTER) >> 8;

        if (toxicTurn < 15)
            ++toxicTurn;
        residualDamage = mon->maxHP / 16;
        if (residualDamage == 0)
            residualDamage = 1;
        damage += residualDamage * toxicTurn;
    }
    else if (status & STATUS1_POISON)
    {
        residualDamage = mon->maxHP / 8;
        damage += residualDamage == 0 ? 1 : residualDamage;
    }

    if (status & STATUS1_BURN)
    {
        residualDamage = mon->maxHP / 8;
        damage += residualDamage == 0 ? 1 : residualDamage;
    }

    if (isActive)
    {
        if ((gStatuses3[battlerId] & STATUS3_LEECHSEED)
         && gBattleMons[gStatuses3[battlerId] & STATUS3_LEECHSEED_BATTLER].hp != 0)
        {
            residualDamage = mon->maxHP / 8;
            damage += residualDamage == 0 ? 1 : residualDamage;
        }
        if ((mon->status2 & STATUS2_NIGHTMARE) && (status & STATUS1_SLEEP))
        {
            residualDamage = mon->maxHP / 4;
            damage += residualDamage == 0 ? 1 : residualDamage;
        }
        if (mon->status2 & STATUS2_CURSED)
        {
            residualDamage = mon->maxHP / 4;
            damage += residualDamage == 0 ? 1 : residualDamage;
        }
        if ((mon->status2 & STATUS2_WRAPPED) > STATUS2_WRAPPED_TURN(1))
        {
            residualDamage = mon->maxHP / 16;
            damage += residualDamage == 0 ? 1 : residualDamage;
        }
    }

    if (!IS_BATTLE_TYPE_GHOST_WITHOUT_SCOPE(gBattleTypeFlags)
     && WEATHER_HAS_EFFECT
     && (((gBattleWeather & B_WEATHER_SANDSTORM)
       && ((gBattleWeather & B_WEATHER_SANDSTORM_PERMANENT) || gWishFutureKnock.weatherDuration > 1)
       && mon->type1 != TYPE_ROCK && mon->type1 != TYPE_STEEL && mon->type1 != TYPE_GROUND
       && mon->type2 != TYPE_ROCK && mon->type2 != TYPE_STEEL && mon->type2 != TYPE_GROUND
       && mon->ability != ABILITY_SAND_VEIL)
      || ((gBattleWeather & B_WEATHER_HAIL)
       && gWishFutureKnock.weatherDuration > 1
       && mon->type1 != TYPE_ICE && mon->type2 != TYPE_ICE)))
    {
        bool8 protectedFromWeather = FALSE;

        if (isActive
         && (gStatuses3[battlerId] & (STATUS3_UNDERGROUND | STATUS3_UNDERWATER)))
            protectedFromWeather = TRUE;
        if (!protectedFromWeather)
        {
            residualDamage = mon->maxHP / 16;
            damage += residualDamage == 0 ? 1 : residualDamage;
        }
    }

    if (mon->item == ITEM_LEFTOVERS && mon->hp < mon->maxHP)
    {
        healing = mon->maxHP / 16;
        if (healing == 0)
            healing = 1;
    }
    if (mon->ability == ABILITY_RAIN_DISH
     && WEATHER_HAS_EFFECT && (gBattleWeather & B_WEATHER_RAIN)
     && mon->hp < mon->maxHP)
    {
        residualDamage = mon->maxHP / 16;
        healing += residualDamage == 0 ? 1 : residualDamage;
    }

    return damage > healing ? damage - healing : 0;
}

static bool8 IsActiveMonSafeAgainstKnownThreats(void)
{
    struct BattlePokemon activeMon = gBattleMons[gActiveBattler];
    u32 expectedDamage = 0;
    u8 numOpponents = 0;
    u8 battlerId;

    for (battlerId = 0; battlerId < MAX_BATTLERS_COUNT; ++battlerId)
    {
        struct PlayerPartyKnowledge *knowledge;
        u8 partyId;
        u8 moveSlot;
        bool8 hasKnownMove = FALSE;
        u32 threat;
        u32 priorityThreat;

        if (GetBattlerSide(battlerId) == GetBattlerSide(gActiveBattler)
         || (gAbsentBattlerFlags & gBitTable[battlerId]))
            continue;

        RememberPlayerBattleKnowledge(battlerId);
        partyId = gBattleStruct->battlerPartyIndexes[battlerId];
        if (partyId >= PARTY_SIZE)
            return FALSE;
        knowledge = &gBattleResources->battleHistory->playerParty[partyId];
        for (moveSlot = 0; moveSlot < MAX_MON_MOVES; ++moveSlot)
        {
            if (knowledge->moves[moveSlot] != MOVE_NONE)
            {
                hasKnownMove = TRUE;
                break;
            }
        }
        if (!hasKnownMove)
            return FALSE;

        threat = GetKnownPlayerThreat(battlerId, &activeMon);
        priorityThreat = GetKnownPlayerPriorityThreat(battlerId, &activeMon, NULL);
        if (priorityThreat > threat)
            threat = priorityThreat;
        expectedDamage += threat;
        if (expectedDamage >= activeMon.hp)
            return FALSE;
        ++numOpponents;
    }

    if (numOpponents == 0)
        return FALSE;

    expectedDamage += GetExpectedEndOfTurnDamage(&activeMon, gActiveBattler, TRUE);
    return expectedDamage < activeMon.hp;
}

static u32 GetInferredStabDamage(struct BattlePokemon *attacker, struct BattlePokemon *defender, u8 attackerId, u8 defenderId)
{
    u32 bestDamage = 0;
    u8 typeIndex;

    for (typeIndex = 0; typeIndex < 2; typeIndex++)
    {
        u8 type = typeIndex == 0 ? attacker->type1 : attacker->type2;
        u16 move;

        if (typeIndex == 1 && type == attacker->type1)
            continue;

        for (move = 1; move < MOVES_COUNT; move++)
        {
            u32 damage;

            if (gBattleMoves[move].effect != EFFECT_HIT
             || gBattleMoves[move].power == 0
             || gBattleMoves[move].type != type)
                continue;

            damage = GetSwitchMoveDamage(attacker, defender, move, 60, attackerId, defenderId);
            if (damage > bestDamage)
                bestDamage = damage;
            break;
        }
    }

    return bestDamage;
}

static bool8 HasEffectiveDamagingMove(struct BattlePokemon *attacker, u8 attackerId, struct BattlePokemon *defender, u8 defenderId)
{
    u8 i;
    u32 damageThreshold = defender->maxHP / 8;

    if (damageThreshold == 0)
        damageThreshold = 1;

    for (i = 0; i < MAX_MON_MOVES; ++i)
    {
        u16 move = attacker->moves[i];
        u32 damage;

        if (move == MOVE_NONE || attacker->pp[i] == 0 || move >= MOVES_COUNT
         || gBattleMoves[move].power == 0
         || gDisableStructs[attackerId].disabledMove == move
         || (gDisableStructs[attackerId].encoredMove != MOVE_NONE
          && gDisableStructs[attackerId].encoredMove != move))
            continue;

        damage = GetSwitchMoveDamage(attacker, defender, move, 0, attackerId, defenderId);
        if (damage >= damageThreshold)
            return TRUE;
    }

    return FALSE;
}

static struct SwitchSpeedRange GetSwitchSpeedRange(struct BattlePokemon *mon, u8 battlerId, bool8 estimatePlayerSpread)
{
    struct SwitchSpeedRange speed;
    u32 stageMultiplier = gStatStageRatios[mon->statStages[STAT_SPEED]][0];
    u32 stageDivisor = gStatStageRatios[mon->statStages[STAT_SPEED]][1];

    if (estimatePlayerSpread)
    {
        u32 baseSpeed = gSpeciesInfo[mon->species].baseSpeed;
        speed.min = ((2 * baseSpeed) * mon->level) / 100 + 5;
        speed.max = ((2 * baseSpeed + 31 + 63) * mon->level) / 100 + 5;
        speed.min = speed.min * 90 / 100;
        speed.max = speed.max * 110 / 100;
    }
    else
    {
        speed.min = mon->speed;
        speed.max = mon->speed;
    }

    if (WEATHER_HAS_EFFECT
     && ((mon->ability == ABILITY_SWIFT_SWIM && (gBattleWeather & B_WEATHER_RAIN))
      || (mon->ability == ABILITY_CHLOROPHYLL && (gBattleWeather & B_WEATHER_SUN))))
    {
        speed.min *= 2;
        speed.max *= 2;
    }

    speed.min = speed.min * stageMultiplier / stageDivisor;
    speed.max = speed.max * stageMultiplier / stageDivisor;
    if (GetBattlerSide(battlerId) == B_SIDE_PLAYER
     && !(gBattleTypeFlags & BATTLE_TYPE_LINK)
     && FlagGet(FLAG_BADGE03_GET))
    {
        speed.min = speed.min * 110 / 100;
        speed.max = speed.max * 110 / 100;
    }
    if (mon->item == ITEM_MACHO_BRACE)
    {
        speed.min /= 2;
        speed.max /= 2;
    }
    if (mon->status1 & STATUS1_PARALYSIS)
    {
        speed.min /= 4;
        speed.max /= 4;
    }

    return speed;
}

static bool8 ShouldSwitchToAvoidSlowerKO(void)
{
    struct BattlePokemon activeMon = gBattleMons[gActiveBattler];
    struct BattlePokemon opponentMon;
    struct BattlePokemon candidate;
    struct SwitchSpeedRange opponentSpeed[MAX_BATTLERS_COUNT];
    u8 opposingBattlers[MAX_BATTLERS_COUNT];
    u32 damageByMonAndOpponent[PARTY_SIZE][MAX_BATTLERS_COUNT];
    u32 threatByMonAndOpponent[PARTY_SIZE][MAX_BATTLERS_COUNT];
    s32 scoreByMon[PARTY_SIZE];
    bool8 validMon[PARTY_SIZE];
    bool8 preserveCounter[PARTY_SIZE];
    u32 activeThreat = 0;
    u32 activeResidualDamage = GetExpectedEndOfTurnDamage(&activeMon, gActiveBattler, TRUE);
    s32 bestScore = -0x7FFFFFFF;
    u8 bestMonId = PARTY_SIZE;
    u8 numOpponents = 0;
    u8 i, j;
    struct SwitchSpeedRange activeSpeed = GetSwitchSpeedRange(&activeMon, gActiveBattler, FALSE);

    for (i = 0; i < PARTY_SIZE; ++i)
    {
        validMon[i] = FALSE;
        preserveCounter[i] = FALSE;
        scoreByMon[i] = -0x7FFFFFFF;
        for (j = 0; j < MAX_BATTLERS_COUNT; ++j)
        {
            damageByMonAndOpponent[i][j] = 0;
            threatByMonAndOpponent[i][j] = 0;
        }
    }

    for (i = 0; i < MAX_BATTLERS_COUNT; ++i)
    {
        if (GetBattlerSide(i) == GetBattlerSide(gActiveBattler)
         || (gAbsentBattlerFlags & gBitTable[i]))
            continue;

        RememberPlayerBattleKnowledge(i);
        LoadKnownPlayerPokemon(&opponentMon, i);
        opponentSpeed[numOpponents] = GetSwitchSpeedRange(&opponentMon, i, TRUE);
        opposingBattlers[numOpponents] = i;
        {
            u32 threat = GetKnownPlayerPriorityThreat(i, &activeMon, NULL);

            if (opponentSpeed[numOpponents].min > activeSpeed.max)
            {
                u32 speedThreat = GetKnownPlayerThreat(i, &activeMon);
                if (speedThreat > threat)
                    threat = speedThreat;
            }
            activeThreat += threat;
            if (activeThreat > activeMon.hp)
                activeThreat = activeMon.hp;
        }
        ++numOpponents;
    }

    activeThreat += activeResidualDamage;
    if (activeThreat > activeMon.hp)
        activeThreat = activeMon.hp;

    if (numOpponents == 0 || activeThreat < activeMon.hp)
        return FALSE;

    for (i = 0; i < PARTY_SIZE; ++i)
    {
        u16 species = GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG);
        u32 incomingDamage = 0;
        u32 outgoingDamage = 0;
        bool8 safelyResists = FALSE;
        bool8 canFasterKO = FALSE;
        bool8 reducesThreat;

        if (species == SPECIES_NONE || species == SPECIES_EGG
         || GetMonData(&gEnemyParty[i], MON_DATA_HP) == 0
         || i == gBattlerPartyIndexes[gActiveBattler])
            continue;
        if ((gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
         && i == gBattlerPartyIndexes[GetBattlerAtPosition(GetBattlerPosition(gActiveBattler) ^ BIT_FLANK)])
            continue;
        if (i == *(gBattleStruct->monToSwitchIntoId + GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT))
         || i == *(gBattleStruct->monToSwitchIntoId + GetBattlerAtPosition(B_POSITION_OPPONENT_RIGHT)))
            continue;

        LoadSwitchCandidate(&candidate, i);
        if (!ApplySwitchInHazards(&candidate))
            continue;
        {
            u8 outgoingAbility;
            u32 originalWeather = ApplySwitchInAbilityEffects(&candidate, gActiveBattler, opposingBattlers, numOpponents, &outgoingAbility);
            u32 candidateResidualDamage = GetExpectedEndOfTurnDamage(&candidate, gActiveBattler, FALSE);
            struct SwitchSpeedRange candidateSpeed = GetSwitchSpeedRange(&candidate, gActiveBattler, FALSE);
            reducesThreat = candidateResidualDamage < activeResidualDamage;

            for (j = 0; j < numOpponents; ++j)
            {
                u8 opponent = opposingBattlers[j];
                u32 threat;
                u32 priorityThreat;
                u32 activeDamage = 0;
                struct BattlePokemon opponentMon;

                LoadKnownPlayerPokemon(&opponentMon, opponent);
                ApplySwitchInIntimidate(&candidate, &opponentMon, opponent);
                threat = GetKnownPlayerThreatWithMon(opponent, &candidate, &opponentMon);
                priorityThreat = GetKnownPlayerPriorityThreatWithMon(opponent, &candidate, NULL, &opponentMon);
                if (priorityThreat > threat)
                    threat = priorityThreat;
                incomingDamage += threat;
                threatByMonAndOpponent[i][j] = threat;

                if (opponentSpeed[j].min > activeSpeed.max)
                    activeDamage = GetKnownPlayerThreat(opponent, &activeMon);
                {
                    u32 priorityDamage = GetKnownPlayerPriorityThreat(opponent, &activeMon, NULL);
                    if (priorityDamage > activeDamage)
                        activeDamage = priorityDamage;
                }
                if (activeDamage != 0 && threat <= activeDamage / 2)
                    reducesThreat = TRUE;
            }

            incomingDamage += candidateResidualDamage;
            safelyResists = incomingDamage < candidate.hp && reducesThreat;

            for (j = 0; j < numOpponents; ++j)
            {
                u8 opponent = opposingBattlers[j];
                u8 moveSlot;
                s8 opponentPriority;
                u32 bestMoveDamage = 0;
                struct BattlePokemon opponentMon;

                LoadKnownPlayerPokemon(&opponentMon, opponent);
                ApplySwitchInIntimidate(&candidate, &opponentMon, opponent);
                GetKnownPlayerPriorityThreatWithMon(opponent, &candidate, &opponentPriority, &opponentMon);
                for (moveSlot = 0; moveSlot < MAX_MON_MOVES; ++moveSlot)
                {
                    u16 move = candidate.moves[moveSlot];
                    u32 damage;

                    if (move == MOVE_NONE || candidate.pp[moveSlot] == 0)
                        continue;
                    if (gBattleMoves[move].priority < opponentPriority
                     || (gBattleMoves[move].priority == opponentPriority
                      && candidateSpeed.min <= opponentSpeed[j].max))
                        continue;

                    damage = GetSwitchMoveDamage(&candidate, &opponentMon, move, 0, gActiveBattler, opponent);
                    if (damage > bestMoveDamage)
                        bestMoveDamage = damage;
                }
                if (bestMoveDamage >= opponentMon.hp
                 && incomingDamage - threatByMonAndOpponent[i][j] < candidate.hp)
                {
                    canFasterKO = TRUE;
                    break;
                }
            }
            gBattleWeather = originalWeather;
            gBattleMons[gActiveBattler].ability = outgoingAbility;
        }

        if (!safelyResists && !canFasterKO)
            continue;

        validMon[i] = TRUE;
        for (j = 0; j < numOpponents; ++j)
        {
            u8 opponent = opposingBattlers[j];
            u8 moveSlot;
            u32 bestMoveDamage = 0;

            LoadKnownPlayerPokemon(&opponentMon, opponent);
            for (moveSlot = 0; moveSlot < MAX_MON_MOVES; ++moveSlot)
            {
                u16 move = candidate.moves[moveSlot];
                u32 damage;

                if (move == MOVE_NONE || candidate.pp[moveSlot] == 0)
                    continue;
                damage = GetSwitchMoveDamage(&candidate, &opponentMon, move, 0, gActiveBattler, opponent);
                if (damage > bestMoveDamage)
                    bestMoveDamage = damage;
            }
            if (bestMoveDamage > opponentMon.hp)
                bestMoveDamage = opponentMon.hp;
            damageByMonAndOpponent[i][j] = bestMoveDamage;
            outgoingDamage += bestMoveDamage;
        }
        scoreByMon[i] = (s32)outgoingDamage - (s32)incomingDamage;
    }

    for (j = 0; j < numOpponents; ++j)
    {
        u8 opponent = opposingBattlers[j];
        u8 opponentPartyId = gBattleStruct->battlerPartyIndexes[opponent];
        u8 strongestCounter = PARTY_SIZE;
        u32 strongestDamage = 0;
        u32 alternativeThreshold;
        u32 significantDamageThreshold;
        u8 alternativeCount = 0;

        if (opponentPartyId >= PARTY_SIZE)
            continue;
        alternativeThreshold = gBattleMons[opponent].maxHP / 8;
        if (alternativeThreshold == 0)
            alternativeThreshold = 1;

        for (i = 0; i < PARTY_SIZE; ++i)
        {
            if (validMon[i] && damageByMonAndOpponent[i][j] > strongestDamage)
            {
                strongestDamage = damageByMonAndOpponent[i][j];
                strongestCounter = i;
            }
        }

        if (strongestCounter == PARTY_SIZE)
            continue;
        significantDamageThreshold = GetMonData(&gEnemyParty[strongestCounter], MON_DATA_MAX_HP) / 4;
        if (significantDamageThreshold == 0)
            significantDamageThreshold = 1;
        if (threatByMonAndOpponent[strongestCounter][j] < significantDamageThreshold)
            continue;

        for (i = 0; i < PARTY_SIZE; ++i)
        {
            if (validMon[i] && i != strongestCounter
             && damageByMonAndOpponent[i][j] >= alternativeThreshold)
                ++alternativeCount;
        }
        if (alternativeCount != 0)
            preserveCounter[strongestCounter] = TRUE;
    }

    for (i = 0; i < PARTY_SIZE; ++i)
    {
        if (!validMon[i] || preserveCounter[i])
            continue;
        if (bestMonId == PARTY_SIZE || scoreByMon[i] > bestScore)
        {
            bestMonId = i;
            bestScore = scoreByMon[i];
        }
    }

    if (bestMonId == PARTY_SIZE)
    {
        for (i = 0; i < PARTY_SIZE; ++i)
        {
            if (!validMon[i])
                continue;
            if (bestMonId == PARTY_SIZE || scoreByMon[i] > bestScore)
            {
                bestMonId = i;
                bestScore = scoreByMon[i];
            }
        }
    }

    if (bestMonId == PARTY_SIZE)
        return FALSE;

    *(gBattleStruct->AI_monToSwitchIntoId + (GetBattlerPosition(gActiveBattler) >> 1)) = bestMonId;
    return TRUE;
}

static void LoadSwitchCandidate(struct BattlePokemon *mon, u8 partyId)
{
    u8 i;
    u16 species = GetMonData(&gEnemyParty[partyId], MON_DATA_SPECIES);
    u8 abilityNum = GetMonData(&gEnemyParty[partyId], MON_DATA_ABILITY_NUM);

    *mon = gBattleMons[gActiveBattler];
    mon->species = species;
    mon->attack = GetMonData(&gEnemyParty[partyId], MON_DATA_ATK);
    mon->defense = GetMonData(&gEnemyParty[partyId], MON_DATA_DEF);
    mon->speed = GetMonData(&gEnemyParty[partyId], MON_DATA_SPEED);
    mon->spAttack = GetMonData(&gEnemyParty[partyId], MON_DATA_SPATK);
    mon->spDefense = GetMonData(&gEnemyParty[partyId], MON_DATA_SPDEF);
    mon->abilityNum = abilityNum;
    mon->ability = gSpeciesInfo[species].abilities[abilityNum];
    mon->type1 = gSpeciesInfo[species].types[0];
    mon->type2 = gSpeciesInfo[species].types[1];
    mon->level = GetMonData(&gEnemyParty[partyId], MON_DATA_LEVEL);
    mon->hp = GetMonData(&gEnemyParty[partyId], MON_DATA_HP);
    mon->maxHP = GetMonData(&gEnemyParty[partyId], MON_DATA_MAX_HP);
    mon->item = GetMonData(&gEnemyParty[partyId], MON_DATA_HELD_ITEM);
    mon->status1 = GetMonData(&gEnemyParty[partyId], MON_DATA_STATUS);
    mon->status2 = 0;
    for (i = 0; i < NUM_BATTLE_STATS; ++i)
        mon->statStages[i] = DEFAULT_STAT_STAGE;
    for (i = 0; i < MAX_MON_MOVES; ++i)
    {
        mon->moves[i] = GetMonData(&gEnemyParty[partyId], MON_DATA_MOVE1 + i);
        mon->pp[i] = GetMonData(&gEnemyParty[partyId], MON_DATA_PP1 + i);
    }
}

static bool8 ApplySwitchInHazards(struct BattlePokemon *mon)
{
    u8 side = GetBattlerSide(gActiveBattler);
    u8 divisor;
    u32 damage;

    if (!(gSideStatuses[side] & SIDE_STATUS_SPIKES)
     || mon->type1 == TYPE_FLYING || mon->type2 == TYPE_FLYING
     || mon->ability == ABILITY_LEVITATE)
        return TRUE;

    if (gSideTimers[side].spikesAmount == 0 || gSideTimers[side].spikesAmount > 3)
        return TRUE;

    divisor = (5 - gSideTimers[side].spikesAmount) * 2;
    damage = mon->maxHP / divisor;
    if (damage == 0)
        damage = 1;
    if (damage >= mon->hp)
        return FALSE;

    mon->hp -= damage;
    return TRUE;
}

static bool8 ShouldStayInAgainstFasterSwitchFallback(u8 partyId)
{
    struct BattlePokemon candidate;
    struct BattlePokemon opponent;
    struct SwitchSpeedRange activeSpeed = GetSwitchSpeedRange(&gBattleMons[gActiveBattler], gActiveBattler, FALSE);
    bool8 foundOpponent = FALSE;
    u8 battlerId;

    LoadSwitchCandidate(&candidate, partyId);
    if (ApplySwitchInHazards(&candidate))
        return FALSE;

    for (battlerId = 0; battlerId < MAX_BATTLERS_COUNT; ++battlerId)
    {
        struct SwitchSpeedRange opponentSpeed;

        if (GetBattlerSide(battlerId) == GetBattlerSide(gActiveBattler)
         || (gAbsentBattlerFlags & gBitTable[battlerId]))
            continue;

        foundOpponent = TRUE;
        LoadKnownPlayerPokemon(&opponent, battlerId);
        opponentSpeed = GetSwitchSpeedRange(&opponent, battlerId, TRUE);
        if (activeSpeed.min <= opponentSpeed.max)
            return FALSE;
    }

    return foundOpponent;
}

static void ApplySwitchInIntimidate(struct BattlePokemon *switchIn, struct BattlePokemon *opponent, u8 opponentId)
{
    if (switchIn->ability != ABILITY_INTIMIDATE
     || opponent->statStages[STAT_ATK] <= MIN_STAT_STAGE
     || (opponent->status2 & STATUS2_SUBSTITUTE)
     || gSideTimers[GetBattlerSide(opponentId)].mistTimer
     || opponent->ability == ABILITY_CLEAR_BODY
     || opponent->ability == ABILITY_HYPER_CUTTER
     || opponent->ability == ABILITY_WHITE_SMOKE)
        return;

    --opponent->statStages[STAT_ATK];
}

static void ApplyForecastType(struct BattlePokemon *mon)
{
    u8 type = TYPE_NORMAL;

    if (mon->species != SPECIES_CASTFORM || mon->ability != ABILITY_FORECAST)
        return;

    if (WEATHER_HAS_EFFECT)
    {
        if (gBattleWeather & B_WEATHER_RAIN)
            type = TYPE_WATER;
        else if (gBattleWeather & B_WEATHER_SUN)
            type = TYPE_FIRE;
        else if (gBattleWeather & B_WEATHER_HAIL)
            type = TYPE_ICE;
    }

    mon->type1 = type;
    mon->type2 = type;
}

static u32 ApplySwitchInAbilityEffects(struct BattlePokemon *switchIn, u8 outgoingBattler, u8 *opposingBattlers, u8 numOpponents, u8 *outgoingAbility)
{
    u32 originalWeather = gBattleWeather;
    u8 i;

    *outgoingAbility = gBattleMons[outgoingBattler].ability;
    if (*outgoingAbility == ABILITY_CLOUD_NINE || *outgoingAbility == ABILITY_AIR_LOCK)
        gBattleMons[outgoingBattler].ability = ABILITY_NONE;

    if (switchIn->ability == ABILITY_TRACE)
    {
        for (i = 0; i < numOpponents; ++i)
        {
            u8 opponentId = opposingBattlers[i];

            if (gBattleMons[opponentId].hp != 0
             && gBattleMons[opponentId].ability != ABILITY_NONE)
            {
                switchIn->ability = gBattleMons[opponentId].ability;
                break;
            }
        }
    }

    switch (switchIn->ability)
    {
    case ABILITY_DRIZZLE:
        if (!(gBattleWeather & B_WEATHER_RAIN_PERMANENT))
            gBattleWeather = B_WEATHER_RAIN_TEMPORARY | B_WEATHER_RAIN_PERMANENT;
        break;
    case ABILITY_SAND_STREAM:
        if (!(gBattleWeather & B_WEATHER_SANDSTORM_PERMANENT))
            gBattleWeather = B_WEATHER_SANDSTORM;
        break;
    case ABILITY_DROUGHT:
        if (!(gBattleWeather & B_WEATHER_SUN_PERMANENT))
            gBattleWeather = B_WEATHER_SUN;
        break;
    case ABILITY_CLOUD_NINE:
    case ABILITY_AIR_LOCK:
        gBattleWeather = 0;
        break;
    }

    ApplyForecastType(switchIn);

    return originalWeather;
}

static void LoadKnownPlayerPokemon(struct BattlePokemon *mon, u8 battlerId)
{
    u8 partyId = gBattleStruct->battlerPartyIndexes[battlerId];

    *mon = gBattleMons[battlerId];
    if (partyId >= PARTY_SIZE)
    {
        mon->ability = ABILITY_NONE;
        mon->item = ITEM_NONE;
        return;
    }

    {
        struct PlayerPartyKnowledge *knowledge = &gBattleResources->battleHistory->playerParty[partyId];

        mon->ability = GetKnownPlayerAbility(knowledge, mon->species);
        if (knowledge->itemKnown)
            mon->item = knowledge->item;
        else
            mon->item = ITEM_NONE;
    }
    ApplyForecastType(mon);
}

static void RememberPlayerBattleKnowledge(u8 battlerId)
{
    u8 partyId = gBattleStruct->battlerPartyIndexes[battlerId];
    struct PlayerPartyKnowledge *knowledge;
    u16 lastMove = gLastMoves[battlerId];
    s32 i;

    if (partyId >= PARTY_SIZE)
        return;

    knowledge = &gBattleResources->battleHistory->playerParty[partyId];
    knowledge->revealed = TRUE;
    if (!knowledge->abilityKnown
     && (gSpeciesInfo[gBattleMons[battlerId].species].abilities[1] == ABILITY_NONE
      || gSpeciesInfo[gBattleMons[battlerId].species].abilities[1]
      == gSpeciesInfo[gBattleMons[battlerId].species].abilities[0]))
    {
        knowledge->ability = gSpeciesInfo[gBattleMons[battlerId].species].abilities[0];
        knowledge->abilityKnown = TRUE;
    }

    if (lastMove != MOVE_NONE && lastMove != MOVE_UNAVAILABLE && lastMove < MOVES_COUNT)
    {
        for (i = 0; i < MAX_MON_MOVES; i++)
        {
            if (knowledge->moves[i] == lastMove)
                break;
            if (knowledge->moves[i] == MOVE_NONE)
            {
                knowledge->moves[i] = lastMove;
                break;
            }
        }
    }

    if (knowledge->itemKnown && knowledge->item != gBattleMons[battlerId].item)
        knowledge->item = gBattleMons[battlerId].item;
}

static u8 GetKnownPlayerAbility(struct PlayerPartyKnowledge *knowledge, u16 species)
{
    if (knowledge->abilityKnown)
        return knowledge->ability;
    if (gSpeciesInfo[species].abilities[1] == ABILITY_NONE
     || gSpeciesInfo[species].abilities[1] == gSpeciesInfo[species].abilities[0])
        return gSpeciesInfo[species].abilities[0];
    return ABILITY_NONE;
}

u8 GetMostSuitableMonToSwitchInto(void)
{
    struct BattlePokemon candidate;
    struct BattlePokemon opponentMon;
    u8 opposingBattlers[MAX_BATTLERS_COUNT];
    u32 damageByMonAndOpponent[PARTY_SIZE][MAX_BATTLERS_COUNT];
    u32 threatByMonAndOpponent[PARTY_SIZE][MAX_BATTLERS_COUNT];
    s32 scoreByMon[PARTY_SIZE];
    bool8 validMon[PARTY_SIZE];
    bool8 preserveCounter[PARTY_SIZE];
    u8 numOpponents = 0;
    u8 bestMonId = PARTY_SIZE;
    u32 bestDamage = 0;
    u32 bestThreat = 0xFFFFFFFF;
    u16 bestHp = 0;
    u8 i, j;
    s32 bestScore = -0x7FFFFFFF;

    if (*(gBattleStruct->monToSwitchIntoId + gActiveBattler) != PARTY_SIZE)
        return *(gBattleStruct->monToSwitchIntoId + gActiveBattler);

    for (i = 0; i < PARTY_SIZE; ++i)
    {
        validMon[i] = FALSE;
        preserveCounter[i] = FALSE;
        scoreByMon[i] = -0x7FFFFFFF;
        for (j = 0; j < MAX_BATTLERS_COUNT; ++j)
        {
            damageByMonAndOpponent[i][j] = 0;
            threatByMonAndOpponent[i][j] = 0;
        }
    }

    for (i = 0; i < MAX_BATTLERS_COUNT; ++i)
    {
        if (GetBattlerSide(i) != GetBattlerSide(gActiveBattler)
         && !(gAbsentBattlerFlags & gBitTable[i]))
        {
            RememberPlayerBattleKnowledge(i);
            opposingBattlers[numOpponents++] = i;
        }
    }

    for (i = 0; i < PARTY_SIZE; ++i)
    {
        u16 species = GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG);
        u32 totalDamage = 0;
        u32 incomingDamage = 0;
        s32 score;
        u32 totalThreat = 0;
        u32 originalWeather;
        u8 outgoingAbility;

        if (species == SPECIES_NONE || species == SPECIES_EGG
         || GetMonData(&gEnemyParty[i], MON_DATA_HP) == 0
         || i == gBattlerPartyIndexes[gActiveBattler])
            continue;

        if ((gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
         && i == gBattlerPartyIndexes[GetBattlerAtPosition(GetBattlerPosition(gActiveBattler) ^ BIT_FLANK)])
            continue;

        if (i == *(gBattleStruct->monToSwitchIntoId + GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT))
         || i == *(gBattleStruct->monToSwitchIntoId + GetBattlerAtPosition(B_POSITION_OPPONENT_RIGHT)))
            continue;
        LoadSwitchCandidate(&candidate, i);
        if (!ApplySwitchInHazards(&candidate))
            continue;
        originalWeather = ApplySwitchInAbilityEffects(&candidate, gActiveBattler, opposingBattlers, numOpponents, &outgoingAbility);

        for (j = 0; j < numOpponents; ++j)
        {
            u8 opponent = opposingBattlers[j];
            u32 bestMoveDamage = 0;
            u32 bestIncomingDamage = 0;
            u8 moveSlot;
            u8 partyId = gBattleStruct->battlerPartyIndexes[opponent];
            struct PlayerPartyKnowledge *knowledge;

            if (partyId >= PARTY_SIZE)
                continue;
            knowledge = &gBattleResources->battleHistory->playerParty[partyId];
            LoadKnownPlayerPokemon(&opponentMon, opponent);
            ApplySwitchInIntimidate(&candidate, &opponentMon, opponent);
            for (moveSlot = 0; moveSlot < MAX_MON_MOVES; ++moveSlot)
            {
                u32 damage;
                u16 move = candidate.moves[moveSlot];

                if (move == MOVE_NONE || candidate.pp[moveSlot] == 0)
                    continue;

                damage = GetSwitchMoveDamage(&candidate, &opponentMon, move, 0, gActiveBattler, opponent);
                if (damage > bestMoveDamage)
                    bestMoveDamage = damage;
            }
            for (moveSlot = 0; moveSlot < MAX_MON_MOVES; ++moveSlot)
            {
                u32 damage;
                u16 move = knowledge->moves[moveSlot];

                if (move == MOVE_NONE || move >= MOVES_COUNT)
                    continue;

                damage = GetSwitchMoveDamage(&opponentMon, &candidate, move, 0, opponent, gActiveBattler);
                if (damage > bestIncomingDamage)
                    bestIncomingDamage = damage;
            }
            {
                u32 inferredDamage = GetInferredStabDamage(&opponentMon, &candidate, opponent, gActiveBattler);
                if (inferredDamage > bestIncomingDamage)
                    bestIncomingDamage = inferredDamage;
            }
            if (bestMoveDamage > opponentMon.hp)
                bestMoveDamage = opponentMon.hp;
            totalDamage += bestMoveDamage;
            totalThreat += bestIncomingDamage;
            damageByMonAndOpponent[i][j] = bestMoveDamage;
            threatByMonAndOpponent[i][j] = bestIncomingDamage;
        }
        incomingDamage = totalThreat;
        incomingDamage += GetExpectedEndOfTurnDamage(&candidate, gActiveBattler, FALSE);

        score = (s32)totalDamage - (s32)incomingDamage;
        validMon[i] = TRUE;
        scoreByMon[i] = score;
        gBattleWeather = originalWeather;
        gBattleMons[gActiveBattler].ability = outgoingAbility;
    }

    for (j = 0; j < numOpponents; ++j)
    {
        u8 opponent = opposingBattlers[j];
        u8 opponentPartyId = gBattleStruct->battlerPartyIndexes[opponent];
        u8 strongestCounter = PARTY_SIZE;
        u32 strongestDamage = 0;
        u32 alternativeThreshold;
        u32 significantDamageThreshold = 1;
        u8 alternativeCount = 0;

        if (opponentPartyId >= PARTY_SIZE)
            continue;

        alternativeThreshold = gBattleMons[opponent].maxHP / 8;
        if (alternativeThreshold == 0)
            alternativeThreshold = 1;

        for (i = 0; i < PARTY_SIZE; ++i)
        {
            if (validMon[i] && damageByMonAndOpponent[i][j] > strongestDamage)
            {
                strongestDamage = damageByMonAndOpponent[i][j];
                strongestCounter = i;
            }
        }

        if (strongestCounter != PARTY_SIZE)
        {
            significantDamageThreshold = GetMonData(&gEnemyParty[strongestCounter], MON_DATA_MAX_HP) / 4;
            if (significantDamageThreshold == 0)
                significantDamageThreshold = 1;
        }

        if (strongestCounter == PARTY_SIZE
         || strongestDamage < alternativeThreshold
         || threatByMonAndOpponent[strongestCounter][j] < significantDamageThreshold)
            continue;

        for (i = 0; i < PARTY_SIZE; ++i)
        {
            if (validMon[i] && i != strongestCounter
             && damageByMonAndOpponent[i][j] >= alternativeThreshold)
                ++alternativeCount;
        }

        if (alternativeCount != 0)
            preserveCounter[strongestCounter] = TRUE;
    }

    bestMonId = PARTY_SIZE;
    bestScore = -0x7FFFFFFF;
    bestDamage = 0;
    bestThreat = 0xFFFFFFFF;
    bestHp = 0;
    for (i = 0; i < PARTY_SIZE; ++i)
    {
        u16 hp;
        u32 damage = 0;
        u32 threat = 0;

        if (!validMon[i] || preserveCounter[i])
            continue;

        for (j = 0; j < numOpponents; ++j)
        {
            damage += damageByMonAndOpponent[i][j];
            threat += threatByMonAndOpponent[i][j];
        }
        hp = validMon[i] ? GetMonData(&gEnemyParty[i], MON_DATA_HP) : 0;

        if (bestMonId == PARTY_SIZE || scoreByMon[i] > bestScore
         || (scoreByMon[i] == bestScore && (damage > bestDamage
          || (damage == bestDamage && (threat < bestThreat
           || (threat == bestThreat && hp > bestHp))))))
        {
            bestMonId = i;
            bestScore = scoreByMon[i];
            bestDamage = damage;
            bestThreat = threat;
            bestHp = hp;
        }
    }

    if (bestMonId == PARTY_SIZE)
    {
        for (i = 0; i < PARTY_SIZE; ++i)
        {
            if (!validMon[i])
                continue;
            if (bestMonId == PARTY_SIZE || scoreByMon[i] > bestScore)
            {
                bestMonId = i;
                bestScore = scoreByMon[i];
            }
        }
    }

    return bestMonId;
}

static u32 GetKnownPlayerThreat(u8 battlerId, struct BattlePokemon *defender)
{
    struct BattlePokemon opponent;

    LoadKnownPlayerPokemon(&opponent, battlerId);
    return GetKnownPlayerThreatWithMon(battlerId, defender, &opponent);
}

static u32 GetKnownPlayerThreatWithMon(u8 battlerId, struct BattlePokemon *defender, struct BattlePokemon *attacker)
{
    struct PlayerPartyKnowledge *knowledge;
    u8 partyId = gBattleStruct->battlerPartyIndexes[battlerId];
    u8 i;
    u32 bestDamage = 0;

    if (partyId >= PARTY_SIZE)
        return 0;

    knowledge = &gBattleResources->battleHistory->playerParty[partyId];
    for (i = 0; i < MAX_MON_MOVES; ++i)
    {
        u16 move = knowledge->moves[i];
        u32 damage;

        if (move == MOVE_NONE || move >= MOVES_COUNT)
            continue;
        damage = GetSwitchMoveDamage(attacker, defender, move, 0, battlerId, gActiveBattler);
        if (damage > bestDamage)
            bestDamage = damage;
    }

    {
        u32 inferredDamage = GetInferredStabDamage(attacker, defender, battlerId, gActiveBattler);
        if (inferredDamage > bestDamage)
            bestDamage = inferredDamage;
    }

    return bestDamage;
}

static u32 GetKnownPlayerPriorityThreat(u8 battlerId, struct BattlePokemon *defender, s8 *priority)
{
    struct BattlePokemon opponent;

    LoadKnownPlayerPokemon(&opponent, battlerId);
    return GetKnownPlayerPriorityThreatWithMon(battlerId, defender, priority, &opponent);
}

static u32 GetKnownPlayerPriorityThreatWithMon(u8 battlerId, struct BattlePokemon *defender, s8 *priority, struct BattlePokemon *attacker)
{
    struct PlayerPartyKnowledge *knowledge;
    u16 possibleMoves[MAX_LEVEL_UP_MOVES];
    u8 partyId = gBattleStruct->battlerPartyIndexes[battlerId];
    u8 i;
    u32 bestDamage = 0;
    s8 highestPriority = 0;

    if (priority != NULL)
        *priority = 0;
    if (partyId >= PARTY_SIZE)
        return 0;

    knowledge = &gBattleResources->battleHistory->playerParty[partyId];
    for (i = 0; i < MAX_MON_MOVES; ++i)
    {
        u16 move = knowledge->moves[i];
        u32 damage;

        if (move == MOVE_NONE || move >= MOVES_COUNT || gBattleMoves[move].priority <= 0
         || gBattleMoves[move].power == 0)
            continue;

        if (gBattleMoves[move].priority > highestPriority)
            highestPriority = gBattleMoves[move].priority;
        damage = GetSwitchMoveDamage(attacker, defender, move, 0, battlerId, gActiveBattler);
        if (damage > bestDamage)
            bestDamage = damage;
    }

    if (gDisableStructs[battlerId].isFirstTurn)
    {
        u8 numMoves = GetLevelUpMovesBySpecies(attacker->species, possibleMoves);

        for (i = 0; i < numMoves; ++i)
        {
            u16 move = possibleMoves[i];
            u8 learnsetIndex;
            bool8 availableAtLevel = FALSE;

            if (move >= MOVES_COUNT || gBattleMoves[move].priority <= 0
             || gBattleMoves[move].power == 0)
                continue;

            for (learnsetIndex = 0;
                 learnsetIndex < MAX_LEVEL_UP_MOVES
              && gLevelUpLearnsets[attacker->species][learnsetIndex] != LEVEL_UP_END;
                 ++learnsetIndex)
            {
                u16 entry = gLevelUpLearnsets[attacker->species][learnsetIndex];

                if ((entry & LEVEL_UP_MOVE_ID) == move
                 && ((entry & LEVEL_UP_MOVE_LV) >> 9) <= attacker->level)
                {
                    availableAtLevel = TRUE;
                    break;
                }
            }
            if (!availableAtLevel)
                continue;

            if (gBattleMoves[move].priority > highestPriority)
                highestPriority = gBattleMoves[move].priority;
            {
                u32 damage = GetSwitchMoveDamage(attacker, defender, move, 0, battlerId, gActiveBattler);
                if (damage > bestDamage)
                    bestDamage = damage;
            }
        }
    }

    if (priority != NULL)
        *priority = highestPriority;
    return bestDamage;
}

static u32 GetKnownPlayerThreatForReserve(u8 partyId, struct BattlePokemon *attacker, u8 defenderId)
{
    struct PlayerPartyKnowledge *knowledge = &gBattleResources->battleHistory->playerParty[partyId];
    u32 bestDamage = 0;
    u8 i;

    for (i = 0; i < MAX_MON_MOVES; ++i)
    {
        u16 move = knowledge->moves[i];
        u32 damage;

        if (move == MOVE_NONE || move >= MOVES_COUNT)
            continue;
        damage = GetSwitchMoveDamage(attacker, &gBattleMons[defenderId], move, 0,
                                     gBattlerTarget, defenderId);
        if (damage > bestDamage)
            bestDamage = damage;
    }

    {
        u32 inferredDamage = GetInferredStabDamage(attacker, &gBattleMons[defenderId],
                                                   gBattlerTarget, defenderId);
        if (inferredDamage > bestDamage)
            bestDamage = inferredDamage;
    }

    return bestDamage;
}

static bool8 IsUsableSwitchAwareMove(u8 moveSlot)
{
    u16 move = gBattleMons[gActiveBattler].moves[moveSlot];

    return move != MOVE_NONE
        && move < MOVES_COUNT
        && gBattleMons[gActiveBattler].pp[moveSlot] != 0
        && gBattleResources->ai->score[moveSlot] != 0
        && gDisableStructs[gActiveBattler].disabledMove != move
        && (gDisableStructs[gActiveBattler].encoredMove == MOVE_NONE
         || gDisableStructs[gActiveBattler].encoredMove == move);
}

static bool8 IsUsefulStatusMove(u16 move, struct BattlePokemon *target, u8 targetBattler)
{
    u16 effect = gBattleMoves[move].effect;
    u8 typeFlags = AI_TypeCalc(move, target->species, target->ability);

    if ((typeFlags & MOVE_RESULT_DOESNT_AFFECT_FOE)
     || (target->status2 & STATUS2_SUBSTITUTE))
        return FALSE;

    switch (effect)
    {
    case EFFECT_SLEEP:
    case EFFECT_YAWN:
        return target->status1 == 0
            && target->ability != ABILITY_INSOMNIA
            && target->ability != ABILITY_VITAL_SPIRIT;
    case EFFECT_POISON:
    case EFFECT_TOXIC:
        return target->status1 == 0
            && target->type1 != TYPE_POISON && target->type1 != TYPE_STEEL
            && target->type2 != TYPE_POISON && target->type2 != TYPE_STEEL
            && target->ability != ABILITY_IMMUNITY;
    case EFFECT_PARALYZE:
        return target->status1 == 0 && target->ability != ABILITY_LIMBER;
    case EFFECT_WILL_O_WISP:
        return target->status1 == 0
            && target->type1 != TYPE_FIRE && target->type2 != TYPE_FIRE
            && target->ability != ABILITY_WATER_VEIL;
    case EFFECT_CONFUSE:
        return !(target->status2 & STATUS2_CONFUSION)
            && target->ability != ABILITY_OWN_TEMPO;
    case EFFECT_LEECH_SEED:
        return target->type1 != TYPE_GRASS && target->type2 != TYPE_GRASS;
    case EFFECT_ATTACK_DOWN:
    case EFFECT_ATTACK_DOWN_2:
        return target->statStages[STAT_ATK] > MIN_STAT_STAGE;
    case EFFECT_DEFENSE_DOWN:
    case EFFECT_DEFENSE_DOWN_2:
        return target->statStages[STAT_DEF] > MIN_STAT_STAGE;
    case EFFECT_SPEED_DOWN:
    case EFFECT_SPEED_DOWN_2:
        return target->statStages[STAT_SPEED] > MIN_STAT_STAGE;
    case EFFECT_SPECIAL_ATTACK_DOWN:
    case EFFECT_SPECIAL_ATTACK_DOWN_2:
        return target->statStages[STAT_SPATK] > MIN_STAT_STAGE;
    case EFFECT_SPECIAL_DEFENSE_DOWN:
    case EFFECT_SPECIAL_DEFENSE_DOWN_2:
        return target->statStages[STAT_SPDEF] > MIN_STAT_STAGE;
    case EFFECT_DISABLE:
        return target != &gBattleMons[targetBattler]
            || (gDisableStructs[targetBattler].disabledMove == MOVE_NONE
             && gLastMoves[targetBattler] != MOVE_NONE
             && gLastMoves[targetBattler] != MOVE_UNAVAILABLE);
    case EFFECT_ENCORE:
        return target != &gBattleMons[targetBattler]
            || (gDisableStructs[targetBattler].encoredMove == MOVE_NONE
             && gLastMoves[targetBattler] != MOVE_NONE
             && gLastMoves[targetBattler] != MOVE_UNAVAILABLE);
    case EFFECT_TAUNT:
        return target != &gBattleMons[targetBattler]
            || gDisableStructs[targetBattler].tauntTimer == 0;
    case EFFECT_TORMENT:
        return !(target->status2 & STATUS2_TORMENT);
    default:
        return FALSE;
    }
}

static u8 GetBestStatusMove(struct BattlePokemon *target, u8 targetBattler, bool8 requireUseful)
{
    u8 bestMove = MAX_MON_MOVES;
    s8 bestScore = -1;
    u8 i;

    for (i = 0; i < MAX_MON_MOVES; ++i)
    {
        u16 move = gBattleMons[gActiveBattler].moves[i];
        s8 score = gBattleResources->ai->score[i];

        if (!IsUsableSwitchAwareMove(i)
         || gBattleMoves[move].power != 0
         || gBattleMoves[move].effect == EFFECT_BIDE
         || gBattleMoves[move].effect == EFFECT_OHKO
         || gBattleMoves[move].effect == EFFECT_PSYWAVE
         || gBattleMoves[move].effect == EFFECT_COUNTER
         || gBattleMoves[move].effect == EFFECT_MIRROR_COAT
         || (requireUseful && !IsUsefulStatusMove(move, target, targetBattler)))
            continue;

        if (bestMove == MAX_MON_MOVES || score > bestScore)
        {
            bestMove = i;
            bestScore = score;
        }
    }

    return bestMove;
}

static bool8 HasKnownPlayerDisruptMove(u8 battlerId)
{
    u8 partyId = gBattleStruct->battlerPartyIndexes[battlerId];
    struct PlayerPartyKnowledge *knowledge;
    u8 i;

    if (partyId >= PARTY_SIZE)
        return FALSE;

    knowledge = &gBattleResources->battleHistory->playerParty[partyId];
    for (i = 0; i < MAX_MON_MOVES; ++i)
    {
        u16 move = knowledge->moves[i];

        if (move == MOVE_NONE || move >= MOVES_COUNT || gBattleMoves[move].power != 0)
            continue;

        switch (gBattleMoves[move].effect)
        {
        case EFFECT_RESTORE_HP:
        case EFFECT_REST:
        case EFFECT_COUNTER:
        case EFFECT_PSYWAVE:
        case EFFECT_MIRROR_COAT:
        case EFFECT_SOFTBOILED:
        case EFFECT_MORNING_SUN:
        case EFFECT_SYNTHESIS:
        case EFFECT_MOONLIGHT:
        case EFFECT_SWALLOW:
        case EFFECT_WISH:
            break;
        default:
            return TRUE;
        }
    }

    return FALSE;
}

static u8 GetTypeEffectivenessMultiplier(u8 attackType, u8 defendType1, u8 defendType2)
{
    u8 multiplier = TYPE_MUL_NORMAL;

    ModulateByTypeEffectiveness(attackType, defendType1, defendType2, &multiplier);
    return multiplier;
}

static u32 GetCounterCoverageScore(u16 move, struct BattlePokemon *activeMon)
{
    u32 score = 0;
    u32 accuracy = gBattleMoves[move].accuracy == 0 ? 100 : gBattleMoves[move].accuracy;
    u32 power = gBattleMoves[move].power;
    u8 type1;
    u8 type2;

    if (gBattleMoves[move].power == 0)
        return 0;

    if (gBattleMoves[move].type == activeMon->type1
     || gBattleMoves[move].type == activeMon->type2)
        power = power * 15 / 10;

    // Unknown reserves are treated as likely counters to the trainer's active Pokemon.
    for (type1 = 0; type1 < NUMBER_OF_MON_TYPES; ++type1)
    {
        if (type1 == TYPE_MYSTERY
         || GetTypeEffectivenessMultiplier(type1, activeMon->type1, activeMon->type2) <= TYPE_MUL_NORMAL)
            continue;

        for (type2 = type1; type2 < NUMBER_OF_MON_TYPES; ++type2)
        {
            u8 effectiveness;

            if (type2 == TYPE_MYSTERY)
                continue;
            effectiveness = GetTypeEffectivenessMultiplier(gBattleMoves[move].type, type1, type2);
            if (effectiveness > TYPE_MUL_NORMAL)
                score += power * accuracy * effectiveness;
        }
    }

    return score;
}

static bool8 IsNeutralDamageMatchup(u32 outgoingDamage, struct BattlePokemon *target,
                                    u32 incomingDamage, struct BattlePokemon *attacker)
{
    u32 outgoingPercent;
    u32 incomingPercent;
    u32 difference;

    if (target->maxHP == 0 || attacker->maxHP == 0)
        return FALSE;

    outgoingPercent = outgoingDamage * 100 / target->maxHP;
    incomingPercent = incomingDamage * 100 / attacker->maxHP;
    difference = outgoingPercent > incomingPercent
               ? outgoingPercent - incomingPercent
               : incomingPercent - outgoingPercent;
    return difference <= 15;
}

void AI_ScoreMovesByOutcome(void)
{
    struct BattlePokemon *attacker;
    struct BattlePokemon *target;
    u32 incomingThreat;
    u32 incomingPercent;
    u8 targetBattler = gBattlerTarget;
    u8 i;

    if (!(gBattleTypeFlags & BATTLE_TYPE_TRAINER)
     || (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
     || GetBattlerSide(targetBattler) != B_SIDE_PLAYER
     || (gAbsentBattlerFlags & gBitTable[targetBattler]))
        return;

    attacker = &gBattleMons[gActiveBattler];
    target = &gBattleMons[targetBattler];
    if (attacker->maxHP == 0 || target->maxHP == 0)
        return;

    incomingThreat = GetKnownPlayerThreat(targetBattler, attacker);
    {
        u32 priorityThreat = GetKnownPlayerPriorityThreat(targetBattler, attacker, NULL);
        if (priorityThreat > incomingThreat)
            incomingThreat = priorityThreat;
    }
    incomingPercent = incomingThreat * 100 / attacker->maxHP;
    if (incomingPercent > 100)
        incomingPercent = 100;

    for (i = 0; i < MAX_MON_MOVES; ++i)
    {
        u16 move = attacker->moves[i];
        s32 score = gBattleResources->ai->score[i];
        u32 damage = 0;
        u32 damagePercent = 0;

        if (!IsUsableSwitchAwareMove(i))
            continue;

        if (gBattleMoves[move].power != 0)
        {
            damage = GetSwitchMoveDamage(attacker, target, move, 0,
                                         gActiveBattler, targetBattler);
            damagePercent = damage * 100 / target->maxHP;
            if (damagePercent > 100)
                damagePercent = 100;
        }

        score = 55 + (score - 100) / 2 + damagePercent / 3;
        if (damage >= target->hp && damage != 0)
        {
            score += 25;
        }
        else
        {
            score -= incomingPercent / 4;
            if (damage == 0)
                score -= incomingPercent / 5;
        }

        if (gBattleMoves[move].effect == EFFECT_PROTECT)
        {
            u32 protectChance = 100;
            u32 residualDamage = GetExpectedEndOfTurnDamage(target, targetBattler, TRUE);
            u32 residualPercent = residualDamage * 100 / target->maxHP;
            u8 protectUses = gDisableStructs[gActiveBattler].protectUses;

            if (protectUses >= 3)
                protectChance = 12;
            else
                protectChance >>= protectUses;

            score += incomingPercent * protectChance / 200;
            score += residualPercent / 2;
            score -= 12;
            if (incomingThreat == 0 && residualDamage == 0)
                score -= 20;
        }

        if (score < 1)
            score = 1;
        else if (score > 127)
            score = 127;
        gBattleResources->ai->score[i] = score;
    }
}

static void LoadRevealedPlayerReserve(struct BattlePokemon *mon, u8 partyId)
{
    struct PlayerPartyKnowledge *knowledge = &gBattleResources->battleHistory->playerParty[partyId];
    u16 species = GetMonData(&gPlayerParty[partyId], MON_DATA_SPECIES);
    u8 level = GetMonData(&gPlayerParty[partyId], MON_DATA_LEVEL);
    u16 actualMaxHP = GetMonData(&gPlayerParty[partyId], MON_DATA_MAX_HP);
    u16 actualHP = GetMonData(&gPlayerParty[partyId], MON_DATA_HP);
    const struct SpeciesInfo *speciesInfo = &gSpeciesInfo[species];
    u8 i;

    *mon = gBattleMons[gActiveBattler];
    mon->species = species;
    mon->attack = ((2 * speciesInfo->baseAttack + 31 + 32 / 4) * level) / 100 + 5;
    mon->defense = ((2 * speciesInfo->baseDefense + 31 + 32 / 4) * level) / 100 + 5;
    mon->speed = ((2 * speciesInfo->baseSpeed + 31 + 32 / 4) * level) / 100 + 5;
    mon->spAttack = ((2 * speciesInfo->baseSpAttack + 31 + 32 / 4) * level) / 100 + 5;
    mon->spDefense = ((2 * speciesInfo->baseSpDefense + 31 + 32 / 4) * level) / 100 + 5;
    mon->level = level;
    mon->maxHP = ((2 * speciesInfo->baseHP + 31 + 32 / 4) * level) / 100 + level + 10;
    mon->hp = actualMaxHP != 0 ? (mon->maxHP * actualHP) / actualMaxHP : mon->maxHP;
    mon->status1 = GetMonData(&gPlayerParty[partyId], MON_DATA_STATUS);
    mon->item = knowledge->itemKnown ? knowledge->item : ITEM_NONE;
    mon->ability = GetKnownPlayerAbility(knowledge, species);
    mon->type1 = gSpeciesInfo[species].types[0];
    mon->type2 = gSpeciesInfo[species].types[1];
    mon->status2 = 0;
    for (i = 0; i < NUM_BATTLE_STATS; ++i)
        mon->statStages[i] = DEFAULT_STAT_STAGE;
}

u8 AI_GetSwitchAwareMoveChoice(void)
{
    struct BattlePokemon *activeOpponent;
    struct BattlePokemon *activeMon;
    struct BattlePokemon predictedSwitch;
    u8 targetBattler = gBattlerTarget;
    u8 predictedSwitchPartyId = PARTY_SIZE;
    u8 healthyReserves = 0;
    u8 unrevealedReserves = 0;
    u8 i, j;
    u8 battlerId;
    u8 traceTargets[MAX_BATTLERS_COUNT];
    u8 numTraceTargets = 0;
    u8 bestActiveMove = MAX_MON_MOVES;
    u8 bestSwitchMove = MAX_MON_MOVES;
    u8 bestStatusMove;
    u8 bestCounterCoverageMove = MAX_MON_MOVES;
    s32 switchLikelihood;
    u32 bestActiveDamage = 0;
    u32 bestSwitchDamage = 0;
    u32 bestCounterCoverage = 0;
    s32 bestSwitchScore = -0x7FFFFFFF;
    u32 activeThreat;
    bool8 hasPredictedSwitchTarget;
    struct SwitchSpeedRange playerSpeed;
    struct SwitchSpeedRange trainerSpeed;

    if (!(gBattleTypeFlags & BATTLE_TYPE_TRAINER)
     || GetBattlerSide(targetBattler) != B_SIDE_PLAYER)
        return MAX_MON_MOVES;

    activeOpponent = &gBattleMons[targetBattler];
    activeMon = &gBattleMons[gActiveBattler];

    for (i = 0; i < MAX_MON_MOVES; ++i)
    {
        u16 move = gBattleMons[gActiveBattler].moves[i];
        u32 damage;

        if (!IsUsableSwitchAwareMove(i) || gBattleMoves[move].power == 0)
            continue;

        damage = GetSwitchMoveDamage(activeMon, activeOpponent, move, 0,
                                     gActiveBattler, targetBattler);
        if (damage > bestActiveDamage)
        {
            bestActiveDamage = damage;
            bestActiveMove = i;
        }
    }

    if (bestActiveMove == MAX_MON_MOVES)
    {
        bestStatusMove = GetBestStatusMove(activeOpponent, targetBattler, FALSE);
        return bestStatusMove;
    }

    for (battlerId = 0; battlerId < MAX_BATTLERS_COUNT; ++battlerId)
    {
        if (GetBattlerSide(battlerId) == GetBattlerSide(gActiveBattler)
         && !(gAbsentBattlerFlags & gBitTable[battlerId]))
            traceTargets[numTraceTargets++] = battlerId;
    }

    for (i = 0; i < PARTY_SIZE; ++i)
    {
        u16 species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES_OR_EGG);

        if (species == SPECIES_NONE || species == SPECIES_EGG
         || GetMonData(&gPlayerParty[i], MON_DATA_HP) == 0
         || i == gBattleStruct->battlerPartyIndexes[targetBattler])
            continue;
        if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
        {
            u8 partner = GetBattlerAtPosition(GetBattlerPosition(targetBattler) ^ BIT_FLANK);
            if (!(gAbsentBattlerFlags & gBitTable[partner])
             && i == gBattleStruct->battlerPartyIndexes[partner])
                continue;
        }
        ++healthyReserves;

        if (!gBattleResources->battleHistory->playerParty[i].revealed)
        {
            ++unrevealedReserves;
            continue;
        }

        LoadRevealedPlayerReserve(&predictedSwitch, i);
        {
            struct BattlePokemon attackerMon = gBattleMons[gActiveBattler];
            u8 outgoingAbility;
            u32 originalWeather = ApplySwitchInAbilityEffects(&predictedSwitch, targetBattler,
                                                               traceTargets, numTraceTargets, &outgoingAbility);
            u32 playerThreat = GetKnownPlayerThreatForReserve(i, &predictedSwitch, gActiveBattler);
            u32 incomingDamage = 0;
            s32 score;

            ApplySwitchInIntimidate(&predictedSwitch, &attackerMon, gActiveBattler);
            ApplyForecastType(&attackerMon);
            for (j = 0; j < MAX_MON_MOVES; ++j)
            {
                u16 move = gBattleMons[gActiveBattler].moves[j];
                u32 damage;

                if (!IsUsableSwitchAwareMove(j) || gBattleMoves[move].power == 0)
                    continue;
                damage = GetSwitchMoveDamage(&attackerMon, &predictedSwitch, move, 0,
                                             gActiveBattler, targetBattler);
                if (damage > incomingDamage)
                    incomingDamage = damage;
            }

            score = (s32)(playerThreat * 100 / gBattleMons[gActiveBattler].maxHP)
                  - (s32)(incomingDamage * 100 / predictedSwitch.maxHP);
            gBattleWeather = originalWeather;
            gBattleMons[targetBattler].ability = outgoingAbility;
            if (predictedSwitchPartyId == PARTY_SIZE || score > bestSwitchScore)
            {
                predictedSwitchPartyId = i;
                bestSwitchScore = score;
            }
        }
    }

    if (healthyReserves == 0)
    {
        activeThreat = GetKnownPlayerThreat(targetBattler, activeMon);
        if (IsNeutralDamageMatchup(bestActiveDamage, activeOpponent, activeThreat, activeMon)
         && HasKnownPlayerDisruptMove(targetBattler))
        {
            bestStatusMove = GetBestStatusMove(activeOpponent, targetBattler, TRUE);
            if (bestStatusMove != MAX_MON_MOVES)
                return bestStatusMove;
        }
        return bestActiveMove;
    }

    activeThreat = GetKnownPlayerThreat(targetBattler, activeMon);
    playerSpeed = GetSwitchSpeedRange(activeOpponent, targetBattler, TRUE);
    trainerSpeed = GetSwitchSpeedRange(activeMon, gActiveBattler, FALSE);
    switchLikelihood = 0;

    if (activeOpponent->maxHP != 0)
    {
        u32 hpPercent = activeOpponent->hp * 100 / activeOpponent->maxHP;

        if (hpPercent <= 50)
            switchLikelihood = 20 + (50 - hpPercent);
    }

    if (switchLikelihood != 0)
    {
        switchLikelihood += healthyReserves * 8;
        if (bestActiveDamage >= activeOpponent->hp)
            switchLikelihood += 15;

        if (playerSpeed.min > trainerSpeed.max && activeThreat >= activeMon->hp / 2)
            switchLikelihood -= 20;
        else if (trainerSpeed.min > playerSpeed.max && bestActiveDamage >= activeOpponent->hp / 2)
            switchLikelihood += 10;

        if (activeThreat < activeMon->hp / 4)
            switchLikelihood += 10;
        if (switchLikelihood > 85)
            switchLikelihood = 85;
        if (switchLikelihood < 0)
            switchLikelihood = 0;
    }

    if (switchLikelihood == 0 || Random() % 100 >= switchLikelihood)
    {
        if (IsNeutralDamageMatchup(bestActiveDamage, activeOpponent, activeThreat, activeMon)
         && HasKnownPlayerDisruptMove(targetBattler))
        {
            bestStatusMove = GetBestStatusMove(activeOpponent, targetBattler, TRUE);
            if (bestStatusMove != MAX_MON_MOVES)
                return bestStatusMove;
        }
        return bestActiveMove;
    }

    hasPredictedSwitchTarget = predictedSwitchPartyId != PARTY_SIZE;
    if (unrevealedReserves != 0)
    {
        for (i = 0; i < MAX_MON_MOVES; ++i)
        {
            u16 move = gBattleMons[gActiveBattler].moves[i];
            u32 coverage;

            if (!IsUsableSwitchAwareMove(i))
                continue;
            coverage = GetCounterCoverageScore(move, activeMon);
            if (coverage > bestCounterCoverage)
            {
                bestCounterCoverage = coverage;
                bestCounterCoverageMove = i;
            }
        }

        if (bestCounterCoverageMove != MAX_MON_MOVES)
            return bestCounterCoverageMove;

        bestStatusMove = GetBestStatusMove(activeOpponent, targetBattler, FALSE);
        if (bestStatusMove != MAX_MON_MOVES)
            return bestStatusMove;
        return bestActiveMove;
    }

    if (hasPredictedSwitchTarget)
    {
        LoadRevealedPlayerReserve(&predictedSwitch, predictedSwitchPartyId);
        bestStatusMove = GetBestStatusMove(&predictedSwitch, targetBattler, FALSE);
    }
    else
    {
        predictedSwitch = *activeOpponent;
        bestStatusMove = GetBestStatusMove(activeOpponent, targetBattler, FALSE);
    }

    for (i = 0; i < MAX_MON_MOVES; ++i)
    {
        u16 move;
        u32 damage;

        if (!IsUsableSwitchAwareMove(i))
            continue;
        move = gBattleMons[gActiveBattler].moves[i];
        if (gBattleMoves[move].power != 0)
        {
            damage = GetSwitchMoveDamage(&gBattleMons[gActiveBattler], &predictedSwitch, move, 0,
                                         gActiveBattler, targetBattler);
            if (damage > bestSwitchDamage)
            {
                bestSwitchDamage = damage;
                bestSwitchMove = i;
            }
        }
    }

    if (bestSwitchMove != MAX_MON_MOVES)
        return bestSwitchMove;
    if (bestStatusMove != MAX_MON_MOVES)
        return bestStatusMove;
    return bestActiveMove;
}

static bool8 ShouldSwitchForBadMatchup(void)
{
    struct BattlePokemon activeMon = gBattleMons[gActiveBattler];
    struct BattlePokemon opponentMon;
    struct BattlePokemon candidate;
    u8 opposingBattlers[MAX_BATTLERS_COUNT];
    u8 numOpponents = 0;
    u8 i, j;
    u32 activeThreat = 0;
    u32 activeOutgoingDamage = 0;
    s32 activeScore;
    s32 bestScore = -0x7FFFFFFF;
    u8 bestMonId = PARTY_SIZE;

    if (activeMon.hp == 0 || HasUsefulStatusOrSetupMove())
        return FALSE;

    for (i = 0; i < MAX_BATTLERS_COUNT; ++i)
    {
        u8 partyId;

        if (GetBattlerSide(i) == GetBattlerSide(gActiveBattler)
         || (gAbsentBattlerFlags & gBitTable[i]))
            continue;

        partyId = gBattleStruct->battlerPartyIndexes[i];
        if (partyId >= PARTY_SIZE)
            continue;
        RememberPlayerBattleKnowledge(i);
        LoadKnownPlayerPokemon(&opponentMon, i);
        opposingBattlers[numOpponents++] = i;

        if (HasEffectiveDamagingMove(&activeMon, gActiveBattler, &opponentMon, i))
            return FALSE;

        activeThreat += GetKnownPlayerThreat(i, &activeMon);
        if (activeThreat > activeMon.hp)
            activeThreat = activeMon.hp;

        {
            u8 moveSlot;
            u32 bestDamage = 0;

            for (moveSlot = 0; moveSlot < MAX_MON_MOVES; ++moveSlot)
            {
                u16 move = activeMon.moves[moveSlot];
                u32 damage;

                if (move == MOVE_NONE || activeMon.pp[moveSlot] == 0
                 || gDisableStructs[gActiveBattler].disabledMove == move
                 || (gDisableStructs[gActiveBattler].encoredMove != MOVE_NONE
                  && gDisableStructs[gActiveBattler].encoredMove != move))
                    continue;
                damage = GetSwitchMoveDamage(&activeMon, &opponentMon, move, 0,
                                             gActiveBattler, i);
                if (damage > bestDamage)
                    bestDamage = damage;
            }
            if (bestDamage > opponentMon.hp)
                bestDamage = opponentMon.hp;
            if (opponentMon.maxHP != 0)
                activeOutgoingDamage += bestDamage * 100 / opponentMon.maxHP;
        }
    }

    if (numOpponents == 0)
        return FALSE;

    activeThreat += GetExpectedEndOfTurnDamage(&activeMon, gActiveBattler, TRUE);
    if (activeThreat > activeMon.hp)
        activeThreat = activeMon.hp;

    activeScore = (s32)activeOutgoingDamage - (s32)(activeThreat * 100 / activeMon.hp);
    for (i = 0; i < PARTY_SIZE; ++i)
    {
        u16 species = GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG);
        u32 outgoingDamage = 0;
        u32 incomingDamage = 0;
        s32 score;
        u32 originalWeather;
        u8 outgoingAbility;

        if (species == SPECIES_NONE || species == SPECIES_EGG
         || GetMonData(&gEnemyParty[i], MON_DATA_HP) == 0
         || i == gBattlerPartyIndexes[gActiveBattler])
            continue;

        if ((gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
         && i == gBattlerPartyIndexes[GetBattlerAtPosition(GetBattlerPosition(gActiveBattler) ^ BIT_FLANK)])
            continue;

        if (i == *(gBattleStruct->monToSwitchIntoId + GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT))
         || i == *(gBattleStruct->monToSwitchIntoId + GetBattlerAtPosition(B_POSITION_OPPONENT_RIGHT)))
            continue;

        LoadSwitchCandidate(&candidate, i);
        if (!ApplySwitchInHazards(&candidate))
            continue;
        originalWeather = ApplySwitchInAbilityEffects(&candidate, gActiveBattler, opposingBattlers, numOpponents, &outgoingAbility);
        for (j = 0; j < numOpponents; ++j)
        {
            u8 opponent = opposingBattlers[j];
            u8 moveSlot;
            u32 bestDamage = 0;

            LoadKnownPlayerPokemon(&opponentMon, opponent);
            ApplySwitchInIntimidate(&candidate, &opponentMon, opponent);
            for (moveSlot = 0; moveSlot < MAX_MON_MOVES; ++moveSlot)
            {
                u16 move = candidate.moves[moveSlot];
                u32 damage;

                if (move == MOVE_NONE || candidate.pp[moveSlot] == 0)
                    continue;
                damage = GetSwitchMoveDamage(&candidate, &opponentMon, move, 0, gActiveBattler, opponent);
                if (damage > bestDamage)
                    bestDamage = damage;
            }
            if (bestDamage > opponentMon.hp)
                bestDamage = opponentMon.hp;
            if (opponentMon.maxHP != 0)
                outgoingDamage += bestDamage * 100 / opponentMon.maxHP;

            incomingDamage += GetKnownPlayerThreatWithMon(opponent, &candidate, &opponentMon);
            if (incomingDamage > candidate.hp)
                incomingDamage = candidate.hp;
        }

        incomingDamage += GetExpectedEndOfTurnDamage(&candidate, gActiveBattler, FALSE);
        score = (s32)outgoingDamage - (s32)(incomingDamage * 100 / candidate.hp);
        if (score > bestScore)
        {
            bestMonId = i;
            bestScore = score;
        }
        gBattleWeather = originalWeather;
        gBattleMons[gActiveBattler].ability = outgoingAbility;
    }

    if (bestMonId != PARTY_SIZE && bestScore > activeScore)
    {
        *(gBattleStruct->AI_monToSwitchIntoId + (GetBattlerPosition(gActiveBattler) >> 1)) = bestMonId;
        return TRUE;
    }
    return FALSE;
}

static bool8 HasUsefulStatusOrSetupMove(void)
{
    u8 battlerSide = GetBattlerSide(gActiveBattler);
    u8 opposingBattler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
    u8 i;

    if (gAbsentBattlerFlags & gBitTable[opposingBattler])
        opposingBattler = GetBattlerAtPosition(B_POSITION_PLAYER_RIGHT);
    if (gAbsentBattlerFlags & gBitTable[opposingBattler])
        return FALSE;

    for (i = 0; i < MAX_MON_MOVES; ++i)
    {
        u16 move = gBattleMons[gActiveBattler].moves[i];
        u16 effect;

        if (move == MOVE_NONE || gBattleMons[gActiveBattler].pp[i] == 0
         || gBattleMoves[move].power != 0
         || gDisableStructs[gActiveBattler].disabledMove == move
         || (gDisableStructs[gActiveBattler].encoredMove != MOVE_NONE
          && gDisableStructs[gActiveBattler].encoredMove != move))
            continue;

        effect = gBattleMoves[move].effect;
        switch (effect)
        {
        case EFFECT_SLEEP:
        case EFFECT_POISON:
        case EFFECT_TOXIC:
        case EFFECT_PARALYZE:
        case EFFECT_CONFUSE:
        case EFFECT_WILL_O_WISP:
        case EFFECT_YAWN:
            if (gBattleMons[opposingBattler].status1 == 0
             && !(gBattleMons[opposingBattler].status2 & STATUS2_CONFUSION))
                return TRUE;
            break;
        case EFFECT_ATTACK_UP:
        case EFFECT_ATTACK_UP_2:
            if (gBattleMons[gActiveBattler].statStages[STAT_ATK] < MAX_STAT_STAGE)
                return TRUE;
            break;
        case EFFECT_DEFENSE_UP:
        case EFFECT_DEFENSE_UP_2:
            if (gBattleMons[gActiveBattler].statStages[STAT_DEF] < MAX_STAT_STAGE)
                return TRUE;
            break;
        case EFFECT_SPEED_UP:
        case EFFECT_SPEED_UP_2:
            if (gBattleMons[gActiveBattler].statStages[STAT_SPEED] < MAX_STAT_STAGE)
                return TRUE;
            break;
        case EFFECT_ACCURACY_UP:
        case EFFECT_ACCURACY_UP_2:
            if (gBattleMons[gActiveBattler].statStages[STAT_ACC] < MAX_STAT_STAGE)
                return TRUE;
            break;
        case EFFECT_EVASION_UP:
        case EFFECT_EVASION_UP_2:
            if (gBattleMons[gActiveBattler].statStages[STAT_EVASION] < MAX_STAT_STAGE)
                return TRUE;
            break;
        case EFFECT_SPECIAL_ATTACK_UP:
        case EFFECT_SPECIAL_ATTACK_UP_2:
            if (gBattleMons[gActiveBattler].statStages[STAT_SPATK] < MAX_STAT_STAGE)
                return TRUE;
            break;
        case EFFECT_SPECIAL_DEFENSE_UP:
        case EFFECT_SPECIAL_DEFENSE_UP_2:
            if (gBattleMons[gActiveBattler].statStages[STAT_SPDEF] < MAX_STAT_STAGE)
                return TRUE;
            break;
        case EFFECT_BELLY_DRUM:
            if (gBattleMons[gActiveBattler].statStages[STAT_ATK] < MAX_STAT_STAGE
             && gBattleMons[gActiveBattler].hp > gBattleMons[gActiveBattler].maxHP / 2)
                return TRUE;
            break;
        case EFFECT_FOCUS_ENERGY:
            if (!(gBattleMons[gActiveBattler].status2 & STATUS2_FOCUS_ENERGY))
                return TRUE;
            break;
        case EFFECT_DEFENSE_CURL:
            if (gBattleMons[gActiveBattler].statStages[STAT_DEF] < MAX_STAT_STAGE)
                return TRUE;
            break;
        case EFFECT_SUBSTITUTE:
            if (!(gBattleMons[gActiveBattler].status2 & STATUS2_SUBSTITUTE)
             && gBattleMons[gActiveBattler].hp > gBattleMons[gActiveBattler].maxHP / 4)
                return TRUE;
            break;
        case EFFECT_REFLECT:
            if (!(gSideStatuses[battlerSide] & SIDE_STATUS_REFLECT))
                return TRUE;
            break;
        case EFFECT_LIGHT_SCREEN:
            if (!(gSideStatuses[battlerSide] & SIDE_STATUS_LIGHTSCREEN))
                return TRUE;
            break;
        case EFFECT_SPIKES:
            if (!(gSideStatuses[GetBattlerSide(opposingBattler)] & SIDE_STATUS_SPIKES))
                return TRUE;
            break;
        case EFFECT_MIST:
            if (!(gSideStatuses[battlerSide] & SIDE_STATUS_MIST))
                return TRUE;
            break;
        case EFFECT_SAFEGUARD:
            if (!(gSideStatuses[battlerSide] & SIDE_STATUS_SAFEGUARD))
                return TRUE;
            break;
        case EFFECT_RAIN_DANCE:
            if (!(gBattleWeather & B_WEATHER_RAIN))
                return TRUE;
            break;
        case EFFECT_SUNNY_DAY:
            if (!(gBattleWeather & B_WEATHER_SUN))
                return TRUE;
            break;
        case EFFECT_SANDSTORM:
            if (!(gBattleWeather & B_WEATHER_SANDSTORM))
                return TRUE;
            break;
        case EFFECT_HAIL:
            if (!(gBattleWeather & B_WEATHER_HAIL))
                return TRUE;
            break;
        case EFFECT_RESTORE_HP:
        case EFFECT_MORNING_SUN:
        case EFFECT_SYNTHESIS:
        case EFFECT_MOONLIGHT:
        case EFFECT_SOFTBOILED:
        case EFFECT_SWALLOW:
            if (gBattleMons[gActiveBattler].hp < gBattleMons[gActiveBattler].maxHP / 2)
                return TRUE;
            break;
        case EFFECT_REST:
            if (gBattleMons[gActiveBattler].hp < gBattleMons[gActiveBattler].maxHP
             && gBattleMons[gActiveBattler].status1 == 0)
                return TRUE;
            break;
        }
    }

    return FALSE;
}

static u8 GetAI_ItemType(u8 itemId, const u8 *itemEffect) // NOTE: should take u16 as item Id argument
{
    if (itemId == ITEM_FULL_RESTORE)
        return AI_ITEM_FULL_RESTORE;
    else if (itemEffect[4] & ITEM4_HEAL_HP)
        return AI_ITEM_HEAL_HP;
    else if (itemEffect[3] & ITEM3_STATUS_ALL)
        return AI_ITEM_CURE_CONDITION;
    else if (itemEffect[0] & (ITEM0_DIRE_HIT | ITEM0_X_ATTACK) || itemEffect[1] != 0 || itemEffect[2] != 0)
        return AI_ITEM_X_STAT;
    else if (itemEffect[3] & ITEM3_GUARD_SPEC)
        return AI_ITEM_GUARD_SPECS;
    else
        return AI_ITEM_NOT_RECOGNIZABLE;
}

static bool8 ShouldUseItem(void)
{
    s32 i;
    u8 validMons = 0;
    bool8 shouldUse = FALSE;

    for (i = 0; i < PARTY_SIZE; ++i)
        if (GetMonData(&gEnemyParty[i], MON_DATA_HP) != 0
         && GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG) != SPECIES_NONE
         && GetMonData(&gEnemyParty[i], MON_DATA_SPECIES_OR_EGG) != SPECIES_EGG)
            ++validMons;
    for (i = 0; i < MAX_TRAINER_ITEMS; ++i)
    {
        u16 item;
        const u8 *itemEffects;
        u8 paramOffset;
        u8 battlerSide;

        if (i && validMons > (gBattleResources->battleHistory->itemsNo - i) + 1)
            continue;
        item = gBattleResources->battleHistory->trainerItems[i];
        if (item == ITEM_NONE || gItemEffectTable[item - ITEM_POTION] == NULL)
            continue;
        if (item == ITEM_ENIGMA_BERRY)
            itemEffects = gSaveBlock1Ptr->enigmaBerry.itemEffect;
        else
            itemEffects = gItemEffectTable[item - ITEM_POTION];
        *(gBattleStruct->AI_itemType + gActiveBattler / 2) = GetAI_ItemType(item, itemEffects);
        switch (*(gBattleStruct->AI_itemType + gActiveBattler / 2))
        {
        case AI_ITEM_FULL_RESTORE:
            if (gBattleMons[gActiveBattler].hp >= gBattleMons[gActiveBattler].maxHP / 4)
                break;
            if (gBattleMons[gActiveBattler].hp == 0)
                break;
            shouldUse = TRUE;
            break;
        case AI_ITEM_HEAL_HP:
            paramOffset = GetItemEffectParamOffset(item, 4, 4);
            if (paramOffset == 0 || gBattleMons[gActiveBattler].hp == 0)
                break;
            if (gBattleMons[gActiveBattler].hp < gBattleMons[gActiveBattler].maxHP / 4 || gBattleMons[gActiveBattler].maxHP - gBattleMons[gActiveBattler].hp > itemEffects[paramOffset])
                shouldUse = TRUE;
            break;
        case AI_ITEM_CURE_CONDITION:
            *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) = 0;
            if (itemEffects[3] & ITEM3_SLEEP && gBattleMons[gActiveBattler].status1 & STATUS1_SLEEP)
            {
                *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) |= 0x20;
                shouldUse = TRUE;
            }
            if (itemEffects[3] & ITEM3_POISON && (gBattleMons[gActiveBattler].status1 & STATUS1_POISON || gBattleMons[gActiveBattler].status1 & STATUS1_TOXIC_POISON))
            {
                *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) |= 0x10;
                shouldUse = TRUE;
            }
            if (itemEffects[3] & ITEM3_BURN && gBattleMons[gActiveBattler].status1 & STATUS1_BURN)
            {
                *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) |= 0x8;
                shouldUse = TRUE;
            }
            if (itemEffects[3] & ITEM3_FREEZE && gBattleMons[gActiveBattler].status1 & STATUS1_FREEZE)
            {
                *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) |= 0x4;
                shouldUse = TRUE;
            }
            if (itemEffects[3] & ITEM3_PARALYSIS && gBattleMons[gActiveBattler].status1 & STATUS1_PARALYSIS)
            {
                *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) |= 0x2;
                shouldUse = TRUE;
            }
            if (itemEffects[3] & ITEM3_CONFUSION && gBattleMons[gActiveBattler].status2 & STATUS2_CONFUSION)
            {
                *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) |= 0x1;
                shouldUse = TRUE;
            }
            break;
        case AI_ITEM_X_STAT:
            *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) = 0;
            if (!gDisableStructs[gActiveBattler].isFirstTurn)
                break;
            if (itemEffects[0] & ITEM0_X_ATTACK)
                *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) |= 0x1;
            if (itemEffects[1] & ITEM1_X_DEFEND)
                *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) |= 0x2;
            if (itemEffects[1] & ITEM1_X_SPEED)
                *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) |= 0x4;
            if (itemEffects[2] & ITEM2_X_SPATK)
                *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) |= 0x8;
            if (itemEffects[2] & ITEM2_X_ACCURACY)
                *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) |= 0x20;
            if (itemEffects[0] & ITEM0_DIRE_HIT)
                *(gBattleStruct->AI_itemFlags + gActiveBattler / 2) |= 0x80;
            shouldUse = TRUE;
            break;
        case AI_ITEM_GUARD_SPECS:
            battlerSide = GetBattlerSide(gActiveBattler);
            if (gDisableStructs[gActiveBattler].isFirstTurn && gSideTimers[battlerSide].mistTimer == 0)
                shouldUse = TRUE;
            break;
        case AI_ITEM_NOT_RECOGNIZABLE:
            return FALSE;
        }
        if (shouldUse)
        {
            BtlController_EmitTwoReturnValues(1, B_ACTION_USE_ITEM, 0);
            *(gBattleStruct->chosenItem + (gActiveBattler / 2) * 2) = item;
            gBattleResources->battleHistory->trainerItems[i] = 0;
            return shouldUse;
        }
    }
    return FALSE;
}
