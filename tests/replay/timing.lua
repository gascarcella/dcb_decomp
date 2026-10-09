-- The emulator's timing trace (issue #26; docs/PORT.md "Busy-waits" and "Testing"): a prelude for
--   .venv/bin/python tests/replay/replay.py run tests/replay/scripts/<x>.json --prelude tests/replay/timing.lua
-- (the prelude adds -debugger; exec breakpoints). It writes $PSXSTACK_REPLAY_OUT/timing.txt, one line per event,
-- each starting with the vsync count (the runner's frames):
--   free <movie frame> <frames shown>   the movie player released a frame (OPEN_decodeMovieFrame's StFreeRing)
--   setloc <lba> | read <sectors> | sync | ready   the loader's CdControlB(Setloc) and CdRead, LIBCD's CdReadSync
--                                                  polls and data-ready callback (one per sector)
--   search | stream | unsetring | write <fd> <bytes>   CdSearchFile, CdRead2, StUnSetRing, the BIOS file write
-- The port's side of the same events is its --trace (StGetNext, cd tick, CdRead, write lines after each tick line).
-- Addresses: config/us/symbols.txt, symbols_openseg.txt.
local ffi = require('ffi')
local mem = PCSX.getMemPtr()
local function s32(a) return ffi.cast('int32_t*', mem + bit.band(a, 0x1FFFFC))[0] end
local function u8(a) return ffi.cast('uint8_t*', mem + bit.band(a, 0x1FFFFF))[0] end
local function bcd(b) return math.floor(b / 16) * 10 + b % 16 end

local OVERLAY_AREA, OPENSEG = 0x801DDF38, 8
local OPEN_MOVIE_FRAME, OPEN_MOVIE_FRAMES_SHOWN = 0x801F0870, 0x801F0848

local out = assert(io.open(assert(os.getenv('PSXSTACK_REPLAY_OUT'), 'PSXSTACK_REPLAY_OUT not set') .. '/timing.txt', 'w'))
local vsync = 0
local function log(s) out:write(vsync .. ' ' .. s .. '\n') end
local function regs() return PCSX.getRegisters().GPR.n end
local function bp(addr, name, fn) return PCSX.addBreakpoint(addr, 'Exec', 4, name, fn) end

-- Kept referenced (a collected listener or breakpoint is removed).
DCB_TIMING_LISTENER = PCSX.Events.createEventListener('GPU::Vsync', function()
    vsync = vsync + 1
    out:flush()
end)
DCB_TIMING_BPS = {
    bp(0x80057F24, 'StFreeRing', function()
        if s32(OVERLAY_AREA) == OPENSEG then
            log(string.format('free %d %d', s32(OPEN_MOVIE_FRAME), s32(OPEN_MOVIE_FRAMES_SHOWN)))
        end
    end),
    bp(0x8005A634, 'CdControlB', function()
        local r = regs()
        if r.a0 == 2 then
            local p = r.a1
            log(string.format('setloc %d', (bcd(u8(p)) * 60 + bcd(u8(p + 1))) * 75 + bcd(u8(p + 2)) - 150))
        end
    end),
    bp(0x8005AED4, 'CdRead', function() log(string.format('read %d', regs().a0)) end),
    bp(0x8005B070, 'CdReadSync', function() log('sync') end),
    bp(0x8005A808, 'CdRead ready', function() log('ready') end),
    bp(0x800572A4, 'CdSearchFile', function() log('search') end),
    bp(0x80057C14, 'CdRead2', function() log('stream') end),
    bp(0x80057D24, 'StUnSetRing', function() log('unsetring') end),
    bp(0x8006A854, 'write', function() local r = regs(); log(string.format('write %d %d', r.a0, r.a2)) end),
}
