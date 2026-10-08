-- The emulator's side of tests/port/vram.py: a prelude run before psxstack's run.lua (tools/replay/run.lua). Each
-- `vram` step of the script makes run.lua append PCSX.GPU.getVRAM() to vram_<name>.bin; this chunk wraps that call so
-- the same frame also writes, beside it in PSXSTACK_REPLAY_OUT:
--   screen_<name>.bin   the displayed picture (PCSX.GPU.takeScreenShot(): 16-bit pixels, or 24-bit bytes)
--   screen_<name>.txt   "<width> <height> <bpp>" (bpp 16 or 24; width in screen pixels)
--   vsyncs_<name>.txt   two lines over the last HISTORY vsyncs, oldest first: the game's FRAME_BUFFER_INDEX (its
--                       render loop flips it once a game frame) and GRAPHICS.vblanksPerFrame (render_loop.c: the
--                       vsyncs its last frame took). A flip on every vsync and 1s throughout mean the game drew every
--                       vsync (not CPU-bound), the condition for comparing the whole VRAM and the frame with the port
-- The name comes from run.lua's io.open of vram_<name>.bin, which comes just before its getVRAM call.
local ffi = require('ffi')
local mem = PCSX.getMemPtr()
local function s32(addr) return ffi.cast('int32_t*', mem + bit.band(addr, 0x1FFFFF))[0] end

local GRAPHICS = 0x800794F8                 -- config/us/symbols.txt
local VBLANKS_PER_FRAME = GRAPHICS + 0x50   -- Graphics.vblanksPerFrame (include/game.h)
local FRAME_BUFFER_INDEX = 0x800794F4
local HISTORY = 120

local out_dir = assert(os.getenv('PSXSTACK_REPLAY_OUT'), 'PSXSTACK_REPLAY_OUT not set')
local index_history, vblank_history, vsync = {}, {}, 0
local dump_name = 'unnamed'

-- Kept referenced (a collected listener is removed: run.lua says the same of its own).
DCB_VRAM_LISTENER = PCSX.Events.createEventListener('GPU::Vsync', function()
    vsync = vsync + 1
    index_history[vsync % HISTORY] = tonumber(s32(FRAME_BUFFER_INDEX))
    vblank_history[vsync % HISTORY] = tonumber(s32(VBLANKS_PER_FRAME))
end)

local io_open = io.open
io.open = function(path, mode)
    local name = type(path) == 'string' and path:match('/vram_(.+)%.bin$')
    if name then dump_name = name end
    return io_open(path, mode)
end

local function write(name, data)
    local f = assert(io_open(out_dir .. '/' .. name, 'wb'))
    f:write(data)
    f:close()
end

local get_vram = PCSX.GPU.getVRAM
PCSX.GPU.getVRAM = function()
    local ss = PCSX.GPU.takeScreenShot()
    local bpp = (tonumber(ss.bpp) == 1) and 24 or 16   -- ScreenShotBPP: BPP_16, BPP_24
    write('screen_' .. dump_name .. '.bin', tostring(ss.data))
    write('screen_' .. dump_name .. '.txt', string.format('%d %d %d\n', ss.width, ss.height, bpp))
    local index, vblanks = {}, {}
    for i = math.max(1, vsync - HISTORY + 1), vsync do
        index[#index + 1] = tostring(index_history[i % HISTORY])
        vblanks[#vblanks + 1] = tostring(vblank_history[i % HISTORY])
    end
    write('vsyncs_' .. dump_name .. '.txt', table.concat(index, ' ') .. '\n' .. table.concat(vblanks, ' ') .. '\n')
    return get_vram()
end
