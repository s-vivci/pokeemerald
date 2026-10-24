#include "global.h"
#include "agent_mailbox.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_interface.h"
#include "battle_setup.h"
#include "battle_util.h"
#include "event_data.h"
#include "main.h"
#include "pokemon.h"
#include "constants/abilities.h"
#include "constants/battle.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/opponents.h"
#include "constants/trainers.h"

#define AGENT_OFFSETOF(type, field) ((u32)&((type *)0)->field)

// The Python side mirrors these offsets; a mismatch must be a compile error, not a silent bug.
STATIC_ASSERT(sizeof(struct AgentEnemyMon) == 0x28, AgentEnemyMonSize)
STATIC_ASSERT(sizeof(struct AgentPlayerMon) == 0x14, AgentPlayerMonSize)
STATIC_ASSERT(AGENT_OFFSETOF(struct AgentMailbox, bridgeFrame) == 0x10, AgentMailboxBridgeFrame)
STATIC_ASSERT(AGENT_OFFSETOF(struct AgentMailbox, trainerId) == 0x1E, AgentMailboxTrainerId)
STATIC_ASSERT(AGENT_OFFSETOF(struct AgentMailbox, fallbacks) == 0x2C, AgentMailboxFallbacks)
STATIC_ASSERT(AGENT_OFFSETOF(struct AgentMailbox, enemy) == 0x30, AgentMailboxEnemy)
STATIC_ASSERT(AGENT_OFFSETOF(struct AgentMailbox, player) == 0x120, AgentMailboxPlayer)
STATIC_ASSERT(sizeof(struct AgentMailbox) == 0x198, AgentMailboxSize)

EWRAM_DATA struct AgentMailbox gAgentMailbox = {0};

// Battle types the agent never handles (the vanilla AI keeps them): doubles and everything
// with special rules or someone else's data.
#define AGENT_UNSUPPORTED_BATTLES (BATTLE_TYPE_DOUBLE | BATTLE_TYPE_LINK | BATTLE_TYPE_MULTI | BATTLE_TYPE_SAFARI \
    | BATTLE_TYPE_FIRST_BATTLE | BATTLE_TYPE_WALLY_TUTORIAL | BATTLE_TYPE_EREADER_TRAINER | BATTLE_TYPE_TWO_OPPONENTS \
    | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_INGAME_PARTNER | BATTLE_TYPE_TOWER_LINK_MULTI | BATTLE_TYPE_RECORDED \
    | BATTLE_TYPE_RECORDED_LINK | BATTLE_TYPE_TRAINER_HILL | BATTLE_TYPE_SECRET_BASE)

static bool8 BridgeAlive(void)
{
    return gAgentMailbox.bridgeFrame != 0
        && (u32)(gMain.vblankCounter1 - gAgentMailbox.bridgeFrame) < AGENT_BRIDGE_TIMEOUT_FRAMES;
}

static bool8 IsPlayerBattler(u8 battler)
{
    return GetBattlerSide(battler) == B_SIDE_PLAYER;
}

void AgentMailbox_OnBattleStart(void)
{
    u32 bridgeFrame = gAgentMailbox.bridgeFrame; // keep the bridge's liveness across battles
    u32 battleId = gAgentMailbox.battleId;
    u32 seq = gAgentMailbox.reqSeq;

    CpuFill32(0, &gAgentMailbox, sizeof(gAgentMailbox));
    gAgentMailbox.magic = AGENT_MAILBOX_MAGIC;
    gAgentMailbox.version = AGENT_MAILBOX_VERSION;
    gAgentMailbox.bridgeFrame = bridgeFrame;
    gAgentMailbox.battleId = battleId + 1;
    gAgentMailbox.reqSeq = seq;       // sequence numbers never go backwards
    gAgentMailbox.respSeq = seq;
}

static void MarkSeen(u8 battler)
{
    u8 slot;
    if (!IsPlayerBattler(battler))
        return;
    slot = gBattlerPartyIndexes[battler];
    if (slot < PARTY_SIZE && gAgentMailbox.player[slot].species == SPECIES_NONE)
    {
        gAgentMailbox.player[slot].species = gBattleMons[battler].species;
        gAgentMailbox.player[slot].level = gBattleMons[battler].level;
    }
}

void AgentMailbox_RevealMove(u8 battler, u16 move)
{
    s32 i;
    u8 slot;
    u16 *moves;

    if (!IsPlayerBattler(battler) || move == MOVE_NONE || move == MOVE_UNAVAILABLE || move == MOVE_STRUGGLE)
        return;
    MarkSeen(battler);
    slot = gBattlerPartyIndexes[battler];
    if (slot >= PARTY_SIZE)
        return;
    moves = gAgentMailbox.player[slot].revealedMoves;
    for (i = 0; i < 4; i++)
    {
        if (moves[i] == move)
            return;
        if (moves[i] == MOVE_NONE)
        {
            moves[i] = move;
            return;
        }
    }
}

void AgentMailbox_RevealAbility(u8 battler, u8 ability)
{
    u8 slot;
    if (!IsPlayerBattler(battler) || ability == ABILITY_NONE)
        return;
    MarkSeen(battler);
    slot = gBattlerPartyIndexes[battler];
    if (slot < PARTY_SIZE)
        gAgentMailbox.player[slot].revealedAbility = ability;
}

void AgentMailbox_RevealItem(u8 battler)
{
    u8 slot;
    u16 item;
    if (!IsPlayerBattler(battler))
        return;
    MarkSeen(battler);
    slot = gBattlerPartyIndexes[battler];
    item = gBattleMons[battler].item ? gBattleMons[battler].item : gLastUsedItem;
    if (slot < PARTY_SIZE && item != ITEM_NONE)
        gAgentMailbox.player[slot].revealedItem = item;
}

void AgentMailbox_CountVanillaItemUse(void)
{
    gAgentMailbox.itemsUsed++;
}

static bool8 IsTrapped(u8 battler)
{
    // Mirrors the B_ACTION_SWITCH checks in HandleTurnActionSelectionState.
    if (gBattleMons[battler].status2 & (STATUS2_WRAPPED | STATUS2_ESCAPE_PREVENTION))
        return TRUE;
    if (gStatuses3[battler] & STATUS3_ROOTED)
        return TRUE;
    if (ABILITY_ON_OPPOSING_FIELD(battler, ABILITY_SHADOW_TAG))
        return TRUE;
    if (ABILITY_ON_OPPOSING_FIELD(battler, ABILITY_ARENA_TRAP)
        && !IS_BATTLER_OF_TYPE(battler, TYPE_FLYING) && gBattleMons[battler].ability != ABILITY_LEVITATE)
        return TRUE;
    if (AbilityBattleEffects(ABILITYEFFECT_CHECK_FIELD_EXCEPT_BATTLER, battler, ABILITY_MAGNET_PULL, 0, 0)
        && IS_BATTLER_OF_TYPE(battler, TYPE_STEEL))
        return TRUE;
    return FALSE;
}

static u8 BuildSwitchMask(u8 battler)
{
    s32 i;
    u8 mask = 0;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (GetMonData(&gEnemyParty[i], MON_DATA_SPECIES) == SPECIES_NONE)
            continue;
        if (GetMonData(&gEnemyParty[i], MON_DATA_IS_EGG))
            continue;
        if (GetMonData(&gEnemyParty[i], MON_DATA_HP) == 0)
            continue;
        if (i == gBattlerPartyIndexes[battler])
            continue;
        mask |= 1 << i;
    }
    return mask;
}

static u8 BuildMoveMask(u8 battler)
{
    s32 i;
    u8 unusable = CheckMoveLimitations(battler, 0, MOVE_LIMITATIONS_ALL);
    u8 mask = 0;
    for (i = 0; i < 4; i++)
    {
        if (gBattleMons[battler].moves[i] != MOVE_NONE && !(unusable & (1 << i)))
            mask |= 1 << i;
    }
    return mask;
}

static u8 BallState(struct Pokemon *mon)
{
    if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE || GetMonData(mon, MON_DATA_IS_EGG))
        return AGENT_BALL_EMPTY;
    if (GetMonData(mon, MON_DATA_HP) == 0)
        return AGENT_BALL_FAINTED;
    if (GetMonData(mon, MON_DATA_STATUS))
        return AGENT_BALL_STATUS;
    return AGENT_BALL_HEALTHY;
}

static void FillParties(void)
{
    s32 i, j;
    u8 badges = 0;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gEnemyParty[i];
        struct AgentEnemyMon *out = &gAgentMailbox.enemy[i];
        u16 species = GetMonData(mon, MON_DATA_SPECIES);

        out->species = species;
        if (species == SPECIES_NONE)
            continue;
        out->item = GetMonData(mon, MON_DATA_HELD_ITEM);
        for (j = 0; j < 4; j++)
        {
            out->moves[j] = GetMonData(mon, MON_DATA_MOVE1 + j);
            out->pp[j] = GetMonData(mon, MON_DATA_PP1 + j);
        }
        out->hp = GetMonData(mon, MON_DATA_HP);
        out->maxHP = GetMonData(mon, MON_DATA_MAX_HP);
        out->stats[0] = GetMonData(mon, MON_DATA_ATK);
        out->stats[1] = GetMonData(mon, MON_DATA_DEF);
        out->stats[2] = GetMonData(mon, MON_DATA_SPEED);
        out->stats[3] = GetMonData(mon, MON_DATA_SPATK);
        out->stats[4] = GetMonData(mon, MON_DATA_SPDEF);
        out->level = GetMonData(mon, MON_DATA_LEVEL);
        out->ability = GetAbilityBySpecies(species, GetMonData(mon, MON_DATA_ABILITY_NUM));
        out->status = GetMonData(mon, MON_DATA_STATUS);
        out->personality = GetMonData(mon, MON_DATA_PERSONALITY);
    }

    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gPlayerParty[i];
        struct AgentPlayerMon *out = &gAgentMailbox.player[i];

        out->ball = BallState(mon);
        if (out->species != SPECIES_NONE) // seen: what the screen showed when it was out
        {
            u16 hp = GetMonData(mon, MON_DATA_HP);
            u16 maxHP = GetMonData(mon, MON_DATA_MAX_HP);
            out->hpBarPixels = maxHP ? GetScaledHPFraction(hp, maxHP, 48) : 0;
            out->status = GetMonData(mon, MON_DATA_STATUS);
        }
    }

    for (i = 0; i < 8; i++)
    {
        if (FlagGet(FLAG_BADGE01_GET + i))
            badges |= 1 << i;
    }
    gAgentMailbox.badges = badges;
}

static bool8 ShouldHandle(u8 battler)
{
    if (gAgentMailbox.magic != AGENT_MAILBOX_MAGIC)
        return FALSE;
    if (!BridgeAlive())
        return FALSE;
    if (GetBattlerSide(battler) != B_SIDE_OPPONENT)
        return FALSE;
    if (!(gBattleTypeFlags & BATTLE_TYPE_TRAINER) || (gBattleTypeFlags & AGENT_UNSUPPORTED_BATTLES))
        return FALSE;
    if (gTrainerBattleOpponent_A == TRAINER_SECRET_BASE)
        return FALSE;
    return TRUE;
}

bool8 AgentMailbox_TryPostRequest(u8 battler, u8 decisionType)
{
    u8 playerBattler;

    if (!ShouldHandle(battler))
        return FALSE;

    playerBattler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
    MarkSeen(playerBattler);

    gAgentMailbox.decisionType = decisionType;
    gAgentMailbox.battler = battler;
    gAgentMailbox.switchMask = (decisionType == AGENT_DECISION_TURN && IsTrapped(battler)) ? 0 : BuildSwitchMask(battler);
    gAgentMailbox.moveMask = (decisionType == AGENT_DECISION_TURN) ? BuildMoveMask(battler) : 0;
    if (gAgentMailbox.moveMask == 0 && gAgentMailbox.switchMask == 0)
        return FALSE; // nothing to decide (or Struggle): let the engine / vanilla AI handle it
    if (decisionType == AGENT_DECISION_TURN && gAgentMailbox.moveMask == 0)
        return FALSE;

    gAgentMailbox.trainerId = gTrainerBattleOpponent_A;
    gAgentMailbox.battleTypeFlags = gBattleTypeFlags;
    gAgentMailbox.turn = gBattleResults.battleTurnCounter;
    gAgentMailbox.enemyActiveSlot = gBattlerPartyIndexes[battler];
    gAgentMailbox.playerActiveSlot = gBattlerPartyIndexes[playerBattler];
    gAgentMailbox.playerPartyCount = CalculatePlayerPartyCount();
    gAgentMailbox.enemyPartyCount = CalculateEnemyPartyCount();
    FillParties();

    gAgentMailbox.respType = AGENT_RESP_NONE;
    gAgentMailbox.respArg = 0;
    gAgentMailbox.reqSeq++;
    gAgentMailbox.romState = AGENT_ROM_REQ_READY; // written last: the request is complete
    return TRUE;
}

u8 AgentMailbox_Poll(u8 *respType, u8 *respArg)
{
    if (gAgentMailbox.bridgeState == AGENT_BRIDGE_RESP_READY && gAgentMailbox.respSeq == gAgentMailbox.reqSeq)
    {
        gAgentMailbox.romState = AGENT_ROM_IDLE;
        *respType = gAgentMailbox.respType;
        *respArg = gAgentMailbox.respArg;
        if (*respType == AGENT_RESP_USE_VANILLA || *respType == AGENT_RESP_NONE)
        {
            gAgentMailbox.fallbacks++;
            return AGENT_POLL_FALLBACK;
        }
        return AGENT_POLL_READY;
    }
    if (!BridgeAlive())
    {
        gAgentMailbox.romState = AGENT_ROM_ABANDONED;
        gAgentMailbox.fallbacks++;
        return AGENT_POLL_FALLBACK;
    }
    return AGENT_POLL_PENDING;
}
