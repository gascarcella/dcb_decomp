-- This game's probes for psxstack's replay runner (psxstack/tools/replay/run.lua and boot_check.lua; GAME_CONTRACT.md
-- "6. Tests"): the same state the port's adapter will report at M1 (game_state_stage/file/map/random_index and the
-- checkpoint image), read from the emulated RAM. Loaded with PSXSTACK_REPLAY, the runner's memory accessors, in
-- scope. Addresses: config/us/symbols*.txt; the layouts: include/game.h, include/dcb/*.h.
--
-- The checkpoint image is the player's profile, PlayerProfile (include/game.h, us: 0x2774 bytes): the pointer-free
-- block a save writes to the memory card (src/openseg/memcard/open_save.c builds the card file from it), so a
-- checkpoint hashes what a save would hold, as dw2003's gamestate_data. It lives in the game's heap: PLAYER_PROFILES
-- (0x8006E050) points at it once initPlayerData has run at boot, always at the same address (the heap's first
-- permanent blocks are allocated in one order), which the scripts assert with a wait_mem before the first checkpoint.
local OVERLAY_AREA = 0x801DDF38     -- the overlay slot (game.json memory.slots[0]); each overlay's first word is its
                                    -- own id: SUGSEG 4, KAWSEG 5, SAISEG 6, SUBSEG 7, OPENSEG 8, EVOSEG 9, ENDSEG 10
local PLAYER_PROFILES = 0x8006E050  -- s32: the two PlayerProfiles' heap address (player_data.c initPlayerData)
local PROFILE_ADDR = 0x800C8964     -- where they land (asserted by the scripts)
local PROFILE_SIZE = 0x2774         -- sizeof(PlayerProfile), us
local PROFILE_AREA_ID = 0x0E        -- PlayerProfile.areaId (u8): the SAISEG area the player is in
local RAND_SEED = 0x801DDC10        -- libc's rand() state (src/main/psyq/libc2_memcpy.c: D_801DDC10, u32)
local PAD_STATES = 0x80089840       -- PadState *PAD_STATES[]: [0] is pad 1; held at +8, pressed at +0xA (s16, the
                                    -- game's own bit layout: include/dcb/pad.h, START 0x800, CROSS 0x40)
local OPENSEG = 8

local R = PSXSTACK_REPLAY
local u8, u16, u32, s32 = R.u8, R.u16, R.u32, R.s32

return {
    image_addr = PROFILE_ADDR,
    image_size = PROFILE_SIZE,
    stage = function() return s32(OVERLAY_AREA) end,
    file = function() return 0 end,                 -- one overlay per stage: nothing to tell apart
    map = function()
        local p = u32(PLAYER_PROFILES)
        if p == 0 then return 0 end
        return u8(p + PROFILE_AREA_ID)
    end,
    random_index = function() return s32(RAND_SEED) end,
    pad_held = function() local p = u32(PAD_STATES); if p == 0 then return 0 end; return u16(p + 8) end,
    pad_pressed = function() local p = u32(PAD_STATES); if p == 0 then return 0 end; return u16(p + 0xA) end,
    -- The boot check (scripts/check_emulator.sh): the executable has loaded OPENSEG for the opening movie
    -- (runMainTask -> playOpeningMovie; about 830 frames with OpenBIOS).
    booted = function() return s32(OVERLAY_AREA) == OPENSEG end,
}
