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
#ifdef TEAL_TEST_BATTLE
#include "teal_test.h"
EWRAM_DATA struct AgentMailbox gAgentMailboxPlayer = {0};
#endif

struct AgentMailbox *AgentMailbox_For(u8 battler)
{
    if (GetBattlerSide(battler) == B_SIDE_OPPONENT)
        return &gAgentMailbox;
#ifdef TEAL_TEST_BATTLE
    return &gAgentMailboxPlayer;
#else
    return NULL;
#endif
}

// The mailbox that tracks what `battler` has revealed: the one serving the other side.
static struct AgentMailbox *WatcherOf(u8 battler)
{
    return AgentMailbox_For(BATTLE_OPPOSITE(battler));
}

static struct Pokemon *PartyOf(u8 battler)
{
    return GetBattlerSide(battler) == B_SIDE_PLAYER ? gPlayerParty : gEnemyParty;
}

// Battle types the agent never handles (the vanilla AI keeps them): doubles and everything
// with special rules or someone else's data.
#define AGENT_UNSUPPORTED_BATTLES (BATTLE_TYPE_DOUBLE | BATTLE_TYPE_LINK | BATTLE_TYPE_MULTI | BATTLE_TYPE_SAFARI \
    | BATTLE_TYPE_FIRST_BATTLE | BATTLE_TYPE_WALLY_TUTORIAL | BATTLE_TYPE_EREADER_TRAINER | BATTLE_TYPE_TWO_OPPONENTS \
    | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_INGAME_PARTNER | BATTLE_TYPE_TOWER_LINK_MULTI | BATTLE_TYPE_RECORDED \
    | BATTLE_TYPE_RECORDED_LINK | BATTLE_TYPE_TRAINER_HILL | BATTLE_TYPE_SECRET_BASE)

static bool8 BridgeAlive(struct AgentMailbox *mb)
{
    return mb->bridgeFrame != 0
        && (u32)(gMain.vblankCounter1 - mb->bridgeFrame) < AGENT_BRIDGE_TIMEOUT_FRAMES;
}

static void ResetMailbox(struct AgentMailbox *mb)
{
    u32 bridgeFrame = mb->bridgeFrame; // keep the bridge's liveness across battles
    u32 battleId = mb->battleId;
    u32 seq = mb->reqSeq;

    CpuFill32(0, mb, sizeof(*mb));
    mb->magic = AGENT_MAILBOX_MAGIC;
    mb->version = AGENT_MAILBOX_VERSION;
    mb->bridgeFrame = bridgeFrame;
    mb->battleId = battleId + 1;
    mb->reqSeq = seq;       // sequence numbers never go backwards
    mb->respSeq = seq;
}

void AgentMailbox_OnBattleStart(void)
{
    ResetMailbox(&gAgentMailbox);
#ifdef TEAL_TEST_BATTLE
    ResetMailbox(&gAgentMailboxPlayer);
#endif
}

static void MarkSeen(u8 battler)
{
    u8 slot;
    struct AgentMailbox *mb = WatcherOf(battler);
    if (mb == NULL)
        return;
    slot = gBattlerPartyIndexes[battler];
    if (slot < PARTY_SIZE && mb->player[slot].species == SPECIES_NONE)
    {
        mb->player[slot].species = gBattleMons[battler].species;
        mb->player[slot].level = gBattleMons[battler].level;
    }
}

void AgentMailbox_RevealMove(u8 battler, u16 move)
{
    s32 i;
    u8 slot;
    u16 *moves;
    struct AgentMailbox *mb = WatcherOf(battler);

    if (mb == NULL || move == MOVE_NONE || move == MOVE_UNAVAILABLE || move == MOVE_STRUGGLE)
        return;
    MarkSeen(battler);
    slot = gBattlerPartyIndexes[battler];
    if (slot >= PARTY_SIZE)
        return;
    moves = mb->player[slot].revealedMoves;
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
    struct AgentMailbox *mb = WatcherOf(battler);
    if (mb == NULL || ability == ABILITY_NONE)
        return;
    MarkSeen(battler);
    slot = gBattlerPartyIndexes[battler];
    if (slot < PARTY_SIZE)
        mb->player[slot].revealedAbility = ability;
}

void AgentMailbox_RevealItem(u8 battler)
{
    u8 slot;
    u16 item;
    struct AgentMailbox *mb = WatcherOf(battler);
    if (mb == NULL)
        return;
    MarkSeen(battler);
    slot = gBattlerPartyIndexes[battler];
    item = gBattleMons[battler].item ? gBattleMons[battler].item : gLastUsedItem;
    if (slot < PARTY_SIZE && item != ITEM_NONE)
        mb->player[slot].revealedItem = item;
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
    struct Pokemon *party = PartyOf(battler);
    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (GetMonData(&party[i], MON_DATA_SPECIES) == SPECIES_NONE)
            continue;
        if (GetMonData(&party[i], MON_DATA_IS_EGG))
            continue;
        if (GetMonData(&party[i], MON_DATA_HP) == 0)
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

static void FillParties(struct AgentMailbox *mb, u8 battler)
{
    s32 i, j;
    u8 badges = 0;
    struct Pokemon *own = PartyOf(battler);
    struct Pokemon *foe = PartyOf(BATTLE_OPPOSITE(battler));

    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &own[i];
        struct AgentEnemyMon *out = &mb->enemy[i];
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
        struct Pokemon *mon = &foe[i];
        struct AgentPlayerMon *out = &mb->player[i];

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
    mb->badges = badges;
}

static bool8 ShouldHandle(struct AgentMailbox *mb, u8 battler)
{
    if (mb == NULL || mb->magic != AGENT_MAILBOX_MAGIC)
        return FALSE;
    if (!BridgeAlive(mb))
        return FALSE;
#ifdef TEAL_TEST_BATTLE
    if (!TealArena_AgentPlays(GetBattlerSide(battler))) // arena: per-side vanilla / agent
        return FALSE;
#else
    if (GetBattlerSide(battler) == B_SIDE_PLAYER)
        return FALSE;
#endif
    if (!(gBattleTypeFlags & BATTLE_TYPE_TRAINER) || (gBattleTypeFlags & AGENT_UNSUPPORTED_BATTLES))
        return FALSE;
    if (gTrainerBattleOpponent_A == TRAINER_SECRET_BASE)
        return FALSE;
    return TRUE;
}

bool8 AgentMailbox_TryPostRequest(u8 battler, u8 decisionType)
{
    u8 foeBattler;
    struct AgentMailbox *mb = AgentMailbox_For(battler);

    if (!ShouldHandle(mb, battler))
        return FALSE;

    foeBattler = BATTLE_OPPOSITE(battler);
    MarkSeen(foeBattler);

    mb->decisionType = decisionType;
    mb->battler = battler;
    mb->switchMask = (decisionType == AGENT_DECISION_TURN && IsTrapped(battler)) ? 0 : BuildSwitchMask(battler);
    mb->moveMask = (decisionType == AGENT_DECISION_TURN) ? BuildMoveMask(battler) : 0;
    if (mb->moveMask == 0 && mb->switchMask == 0)
        return FALSE; // nothing to decide (or Struggle): let the engine / vanilla AI handle it
    if (decisionType == AGENT_DECISION_TURN && mb->moveMask == 0)
        return FALSE;

    mb->trainerId = gTrainerBattleOpponent_A;
    mb->battleTypeFlags = gBattleTypeFlags;
    mb->turn = gBattleResults.battleTurnCounter;
    mb->enemyActiveSlot = gBattlerPartyIndexes[battler];       // own side
    mb->playerActiveSlot = gBattlerPartyIndexes[foeBattler];   // other side
    if (GetBattlerSide(battler) == B_SIDE_OPPONENT)
    {
        mb->playerPartyCount = CalculatePlayerPartyCount();
        mb->enemyPartyCount = CalculateEnemyPartyCount();
    }
    else
    {
        mb->playerPartyCount = CalculateEnemyPartyCount();
        mb->enemyPartyCount = CalculatePlayerPartyCount();
    }
    FillParties(mb, battler);

    mb->respType = AGENT_RESP_NONE;
    mb->respArg = 0;
    mb->reqSeq++;
    mb->romState = AGENT_ROM_REQ_READY; // written last: the request is complete
    return TRUE;
}

u8 AgentMailbox_Poll(u8 battler, u8 *respType, u8 *respArg)
{
    struct AgentMailbox *mb = AgentMailbox_For(battler);

    if (mb == NULL)
        return AGENT_POLL_FALLBACK;
    if (mb->bridgeState == AGENT_BRIDGE_RESP_READY && mb->respSeq == mb->reqSeq)
    {
        mb->romState = AGENT_ROM_IDLE;
        *respType = mb->respType;
        *respArg = mb->respArg;
        if (*respType == AGENT_RESP_USE_VANILLA || *respType == AGENT_RESP_NONE)
        {
            mb->fallbacks++;
            return AGENT_POLL_FALLBACK;
        }
        return AGENT_POLL_READY;
    }
    if (!BridgeAlive(mb))
    {
        mb->romState = AGENT_ROM_ABANDONED;
        mb->fallbacks++;
        return AGENT_POLL_FALLBACK;
    }
    return AGENT_POLL_PENDING;
}
