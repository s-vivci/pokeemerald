#ifndef GUARD_TEAL_TEST_H
#define GUARD_TEAL_TEST_H

// Test-only boot path for Pokemon Teal (off by default).
// Build with `make TEAL_TEST=1` to get a ROM that skips the intro and loops through
// trainer battles, for headless tests:
//  * legacy loop: a fixed list of trainers against a fixed Marshtomp the human (or the
//    A-tapping test driver) plays;
//  * arena: when a calibration runner answers the handshake in gTealArena, each battle is
//    a mirror match (both sides get a trainer's party) and each side is played by the
//    vanilla AI or by the agent through its mailbox.
void TealTest_Start(void);

#ifdef TEAL_TEST_BATTLE

#define TEAL_ARENA_MAGIC 0x4E524154 // "TARN"

// state
#define TEAL_ARENA_IDLE    0
#define TEAL_ARENA_WAITING 1 // ROM: give me the next battle
#define TEAL_ARENA_READY   2 // runner: config written
#define TEAL_ARENA_RUNNING 3

// playerMode / enemyMode
#define TEAL_ARENA_VANILLA 0
#define TEAL_ARENA_AGENT   1

struct TealArena
{
    /*0x00*/ u32 magic;
    /*0x04*/ u8 state;
    /*0x05*/ u8 playerMode;
    /*0x06*/ u8 enemyMode;
    /*0x07*/ u8 maxTurns;      // 0 = no cap; else the battle ends as a draw after this many turns
    /*0x08*/ u16 enemyTrainer;
    /*0x0A*/ u16 playerTrainer;
    /*0x0C*/ u32 seed;
    /*0x10*/ u32 battleCount;  // battles started in arena mode
    /*0x14*/ u8 lastOutcome;   // gBattleOutcome of the previous battle
    /*0x15*/ u8 lastTurns;
    /*0x16*/ u8 configured;    // a runner has answered at least once
    /*0x17*/ u8 active;        // the current battle is an arena battle
    /*0x18*/ u32 waitFrames;
}; // 0x1C

extern struct TealArena gTealArena;

bool8 TealArena_Active(void);
bool8 TealArena_AgentPlays(u8 side);    // who decides for that side: the agent (TRUE) or vanilla
bool8 TealArena_AgentPlaysPlayer(void);
bool8 TealArena_TurnCapReached(void);

// battle_controller_player.c: the player's side decides like an AI in arena battles
bool8 TealArena_PlayerChooseAction(void);
bool8 TealArena_PlayerChooseMove(void);
bool8 TealArena_PlayerChoosePokemon(void);

// battle_main.c
u8 TealArena_CreatePlayerParty(u16 trainerNum);

#endif // TEAL_TEST_BATTLE

#endif // GUARD_TEAL_TEST_H
