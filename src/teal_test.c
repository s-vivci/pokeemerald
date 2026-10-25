// Pokemon Teal: test-only boot-into-battle loop. Compiled to nothing unless
// TEAL_TEST_BATTLE is defined (make TEAL_TEST=1). Never ship this build.
#include "global.h"
#include "teal_test.h"

#ifdef TEAL_TEST_BATTLE
#include "battle.h"
#include "battle_setup.h"
#include "event_data.h"
#include "main.h"
#include "pokemon.h"
#include "random.h"
#include "script_pokemon_util.h"
#include "string_util.h"
#include "constants/battle.h"
#include "constants/flags.h"
#include "constants/global.h"
#include "constants/moves.h"
#include "constants/opponents.h"
#include "constants/species.h"

static const u16 sTestTrainers[] = {
    TRAINER_CALVIN_1,   // route 102, one Poochyena
    TRAINER_ROXANNE_1,  // potions, Sturdy, forced switches
    TRAINER_JOSH,       // Rustboro gym trainee
    TRAINER_BRAWLY_1,   // super potions
    TRAINER_WATTSON_1,  // Magnemite: Magnet Pull is not a trap for a water type
    TRAINER_SIDNEY,     // Elite Four: the player loses quickly
};

static const u8 sTestPlayerName[] = _("TEAL");

EWRAM_DATA static u16 sTestBattleCount = 0;
EWRAM_DATA struct TealArena gTealArena = {0};

// Frames to wait for a calibration runner before falling back to the legacy loop.
#define ARENA_HANDSHAKE_FRAMES 180

static void TealTest_NextBattle(void);
static void LegacyNextBattle(void);

bool8 TealArena_Active(void)
{
    return gTealArena.active;
}

bool8 TealArena_AgentPlays(u8 side)
{
    if (!gTealArena.active)
        return side == B_SIDE_OPPONENT; // legacy loop: the agent plays the trainer, a human the player
    if (side == B_SIDE_PLAYER)
        return gTealArena.playerMode == TEAL_ARENA_AGENT;
    return gTealArena.enemyMode == TEAL_ARENA_AGENT;
}

bool8 TealArena_AgentPlaysPlayer(void)
{
    return TealArena_AgentPlays(B_SIDE_PLAYER);
}

bool8 TealArena_AIPlaysPlayer(void)
{
    return gTealArena.active && gTealArena.playerMode != TEAL_ARENA_HUMAN;
}

bool8 TealArena_TurnCapReached(void)
{
    return gTealArena.active && gTealArena.maxTurns != 0
        && gBattleResults.battleTurnCounter + 1 >= gTealArena.maxTurns;
}

static void ClearBadges(void)
{
    u8 i;
    for (i = 0; i < 8; i++)
        FlagClear(FLAG_BADGE01_GET + i); // no badge stat boosts for the player's side
}

static void StartArenaBattle(void)
{
    gTealArena.state = TEAL_ARENA_RUNNING;
    gTealArena.active = TRUE;
    gTealArena.battleCount++;
    ClearBadges();
    gBattleTypeFlags = BATTLE_TYPE_TRAINER;
    gTrainerBattleOpponent_A = gTealArena.enemyTrainer;
    ZeroPlayerPartyMons();
    TealArena_CreatePlayerParty(gTealArena.playerTrainer);
    CalculatePlayerPartyCount();
    gRngValue = gTealArena.seed;
    gSaveBlock2Ptr->optionsBattleSceneOff = !(gTealArena.flags & TEAL_ARENA_FLAG_ANIMATIONS);
    // The overworld's battle transition normally starts the music; the arena skips it, so start
    // the trainer's battle theme here (Elite Four, gym leader, rival... by trainer class).
    PlayBattleBGM();
    gMain.savedCallback = TealTest_NextBattle;
    SetMainCallback2(CB2_InitBattle);
}

static void CB2_ArenaWait(void)
{
    if (gTealArena.state == TEAL_ARENA_READY)
    {
        gTealArena.configured = TRUE;
        StartArenaBattle();
        return;
    }
    gTealArena.waitFrames++;
    if (!gTealArena.configured && gTealArena.waitFrames > ARENA_HANDSHAKE_FRAMES)
    {
        // Nobody runs the arena (yet): play one battle of the old loop, then ask again, so a
        // runner that connects late (mGBA window: scripts loaded after the ROM) takes over.
        gTealArena.state = TEAL_ARENA_IDLE;
        LegacyNextBattle();
    }
}

static void GiveMon(u8 slot, u16 species, u8 level, u16 m0, u16 m1, u16 m2, u16 m3)
{
    struct Pokemon *mon = &gPlayerParty[slot];
    CreateMon(mon, species, level, 31, TRUE, slot + 1, OT_ID_PLAYER_ID, 0);
    SetMonMoveSlot(mon, m0, 0);
    SetMonMoveSlot(mon, m1, 1);
    SetMonMoveSlot(mon, m2, 2);
    SetMonMoveSlot(mon, m3, 3);
}

static void TealTest_NextBattle(void)
{
    gTealArena.lastOutcome = gBattleOutcome;
    gTealArena.lastTurns = gBattleResults.battleTurnCounter;
    gTealArena.active = FALSE;
    // Ask the runner (via the Lua bridge) for the next battle; nothing runs meanwhile.
    SetVBlankCallback(NULL);
    SetHBlankCallback(NULL);
    gTealArena.magic = TEAL_ARENA_MAGIC;
    gTealArena.waitFrames = 0;
    gTealArena.state = TEAL_ARENA_WAITING;
    SetMainCallback2(CB2_ArenaWait);
}

static void LegacyNextBattle(void)
{
    u8 i;

    ZeroPlayerPartyMons();
    GiveMon(0, SPECIES_MARSHTOMP, 30, MOVE_WATER_GUN, MOVE_MUD_SHOT, MOVE_TACKLE, MOVE_PROTECT);
    CalculatePlayerPartyCount();
    HealPlayerParty();
    gBattleTypeFlags = BATTLE_TYPE_TRAINER;
    gTrainerBattleOpponent_A = sTestTrainers[sTestBattleCount % ARRAY_COUNT(sTestTrainers)];
    // Badges grow with the loop, so the badge fields of the mailbox change too.
    for (i = 0; i < 8; i++)
    {
        if (i < (sTestBattleCount % 9))
            FlagSet(FLAG_BADGE01_GET + i);
        else
            FlagClear(FLAG_BADGE01_GET + i);
    }
    sTestBattleCount++;
    gMain.savedCallback = TealTest_NextBattle;
    SetMainCallback2(CB2_InitBattle);
}

void TealTest_Start(void)
{
    // A fresh save has no player name (no EOS), and battle text that prints it overflows.
    StringCopy(gSaveBlock2Ptr->playerName, sTestPlayerName);
    // SET style: no "will you switch?" prompt, so tapping A always means FIGHT -> move 1.
    gSaveBlock2Ptr->optionsBattleStyle = OPTIONS_BATTLE_STYLE_SET;
    gSaveBlock2Ptr->optionsBattleSceneOff = TRUE;
    gSaveBlock2Ptr->optionsTextSpeed = OPTIONS_TEXT_SPEED_FAST;
    // Legacy loop: one mon only. When it faints the battle is lost and the loop moves on, so
    // the A-tapping driver never has to work the party menu.
    sTestBattleCount = 0;
    gBattleOutcome = 0;
    gBattleResults.battleTurnCounter = 0;
    TealTest_NextBattle();
}

#else

void TealTest_Start(void)
{
}

#endif // TEAL_TEST_BATTLE
