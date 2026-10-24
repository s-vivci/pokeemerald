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

static void TealTest_NextBattle(void);

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
    u8 i;

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
    ZeroPlayerPartyMons();
    GiveMon(0, SPECIES_MARSHTOMP, 30, MOVE_WATER_GUN, MOVE_MUD_SHOT, MOVE_TACKLE, MOVE_PROTECT);
    // One mon only: when it faints the battle is lost and the loop moves on, so the A-tapping
    // driver never has to work the party menu.
    CalculatePlayerPartyCount();
    sTestBattleCount = 0;
    TealTest_NextBattle();
}

#else

void TealTest_Start(void)
{
}

#endif // TEAL_TEST_BATTLE
