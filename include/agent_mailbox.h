#ifndef GUARD_AGENT_MAILBOX_H
#define GUARD_AGENT_MAILBOX_H

// Pokemon Teal: shared-memory mailbox between the battle engine and an external agent
// (mGBA Lua bridge -> Python). The ROM posts a request when an enemy trainer must decide,
// keeps the game running frame by frame while it waits, and falls back to the vanilla AI
// whenever the bridge is absent, too slow to show signs of life, or answers illegally.
//
// Fairness: the player's side is exported only as revealed information (moves the player's
// mons have used, abilities and items that activated) plus what is on screen (species,
// level, HP bar pixels, status, party balls). Layout is mirrored in agent/teal/rom/mailbox.py.

#define AGENT_MAILBOX_MAGIC   0x424D4941 // "AIMB"
#define AGENT_MAILBOX_VERSION 1

// romState
#define AGENT_ROM_IDLE      0
#define AGENT_ROM_REQ_READY 1
#define AGENT_ROM_ABANDONED 2

// bridgeState (written by the bridge)
#define AGENT_BRIDGE_IDLE       0
#define AGENT_BRIDGE_ACK        1
#define AGENT_BRIDGE_RESP_READY 2

// decisionType
#define AGENT_DECISION_TURN          1 // choose a move or a switch
#define AGENT_DECISION_FORCED_SWITCH 2 // choose a replacement after a faint / Baton Pass

// respType
#define AGENT_RESP_NONE        0
#define AGENT_RESP_MOVE        1 // respArg = move slot 0-3
#define AGENT_RESP_SWITCH      2 // respArg = party slot 0-5
#define AGENT_RESP_USE_VANILLA 3

// AgentMailbox_Poll results
#define AGENT_POLL_PENDING  0
#define AGENT_POLL_READY    1
#define AGENT_POLL_FALLBACK 2

// The bridge copies gMain.vblankCounter1 into bridgeFrame every frame. If that copy is older
// than this, the bridge is considered gone and the vanilla AI decides.
#define AGENT_BRIDGE_TIMEOUT_FRAMES 60

// player party ball states (what the party indicator shows)
#define AGENT_BALL_EMPTY   0
#define AGENT_BALL_HEALTHY 1
#define AGENT_BALL_STATUS  2
#define AGENT_BALL_FAINTED 3

struct AgentEnemyMon
{
    /*0x00*/ u16 species;
    /*0x02*/ u16 item;
    /*0x04*/ u16 moves[4];
    /*0x0C*/ u8 pp[4];
    /*0x10*/ u16 hp;
    /*0x12*/ u16 maxHP;
    /*0x14*/ u16 stats[5]; // atk, def, spe, spa, spd
    /*0x1E*/ u8 level;
    /*0x1F*/ u8 ability;
    /*0x20*/ u32 status;
    /*0x24*/ u32 personality;
}; // 0x28

struct AgentPlayerMon
{
    /*0x00*/ u16 species;       // 0 until this mon has been seen in battle
    /*0x02*/ u16 revealedItem;
    /*0x04*/ u16 revealedMoves[4];
    /*0x0C*/ u8 level;          // 0 until seen
    /*0x0D*/ u8 revealedAbility;
    /*0x0E*/ u8 hpBarPixels;    // 0-48, only once seen
    /*0x0F*/ u8 ball;           // AGENT_BALL_*, always (the party indicator shows it)
    /*0x10*/ u32 status;        // only once seen
}; // 0x14

struct AgentMailbox
{
    /*0x000*/ u32 magic;
    /*0x004*/ u16 version;
    /*0x006*/ u8 romState;
    /*0x007*/ u8 bridgeState;
    /*0x008*/ u32 reqSeq;
    /*0x00C*/ u32 respSeq;
    /*0x010*/ u32 bridgeFrame;
    /*0x014*/ u32 battleId;
    /*0x018*/ u8 decisionType;
    /*0x019*/ u8 battler;
    /*0x01A*/ u8 moveMask;
    /*0x01B*/ u8 switchMask;
    /*0x01C*/ u8 respType;
    /*0x01D*/ u8 respArg;
    /*0x01E*/ u16 trainerId;
    /*0x020*/ u32 battleTypeFlags;
    /*0x024*/ u8 badges;          // bit n = badge n+1
    /*0x025*/ u8 turn;
    /*0x026*/ u8 enemyActiveSlot;
    /*0x027*/ u8 playerActiveSlot;
    /*0x028*/ u8 playerPartyCount;
    /*0x029*/ u8 enemyPartyCount;
    /*0x02A*/ u16 itemsUsed;      // vanilla item uses this battle (items stay vanilla for now)
    /*0x02C*/ u32 fallbacks;      // decisions handed back to the vanilla AI this battle
    /*0x030*/ struct AgentEnemyMon enemy[6];
    /*0x120*/ struct AgentPlayerMon player[6];
}; // 0x198

extern struct AgentMailbox gAgentMailbox;

void AgentMailbox_OnBattleStart(void);
bool8 AgentMailbox_TryPostRequest(u8 battler, u8 decisionType);
u8 AgentMailbox_Poll(u8 *respType, u8 *respArg);
void AgentMailbox_RevealMove(u8 battler, u16 move);
void AgentMailbox_RevealAbility(u8 battler, u8 ability);
void AgentMailbox_RevealItem(u8 battler);
void AgentMailbox_CountVanillaItemUse(void);

#endif // GUARD_AGENT_MAILBOX_H
