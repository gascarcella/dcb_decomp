-- SPU register-write trace recorder (tests/sound/spu_trace.py; docs/PORT.md "Sound"). Adapted from dw2003recomp's
-- tests/sound/spu_trace.lua (docs/THIRD_PARTY.md): loaded into PCSX-Redux before psxstack's run.lua (spu_trace.py writes
-- a wrapper chunk that does dofile(this), then dofile(run.lua)). Needs -debugger -interpreter: memory and exec
-- breakpoints never fire under the dynarec (this game runs on the interpreter anyway: tests/replay/replay.py).
--
-- Input (DCB_SPU_TRACE_SPEC, a Lua chunk spu_trace.py writes):
--   return { calls = { { addr = 0x80050C40, name = 'SsUtKeyOnV', dump = 'chunk' | <bytes> }, ... },  -- logged
--            counts = { { addr = 0x8004F500, name = '_SsSeqPlay' }, ... } }                         -- counted only
-- Output (DCB_SPU_TRACE_OUT, a directory), written when the runner calls PCSX.quit:
--   raw.txt   one line per event, in emulated order:
--             "w <tick> <addr> <width> <value> <pc>"   a CPU store to 0x1F801C00..0x1F801DFF (SPU) or 0x1F8010C0..CF (DMA4)
--             "d <tick> <madr> <bcr> <chcr> <offset> <length>"   a DMA4 start from RAM to the SPU: <length> bytes of RAM
--                                                      at <madr>, stored at <offset> in data.bin
--             "c <tick> <name> <a0> <a1> <a2> <a3> <s0> <s1> <s2> <s3> <ra> [<dump>]"   a call of a logged function
--                                                      (hex but the tick): the four argument registers, the four
--                                                      stack words after the callee's home space (arguments 5-8 of a
--                                                      function that takes more than four: SsUtKeyOnV), the return
--                                                      address and, per the spec, what a0 points at:
--                                                      "chunk=<offset>:<length>" (a PAK chunk: its length is the
--                                                      s32 at a0 - 4, the chunk header's size; the bytes in data.bin)
--                                                      or "mem=<hex>" (<bytes> bytes, a struct: SpuVoiceAttr)
--                                                      or "cycles=<n>" (the CPU's cycle count: SsSeqCalledTbyT)
--             "m <tick> exe_start" / "m <tick> reset"     the game's EXE starts / the console is reset
--   data.bin  the DMA4 source blocks and the chunks, back to back (spu_trace.py hashes them and deletes the file)
--   counts.txt "<name> <calls>" per counted or logged function
-- <tick> is the vsync count (GPU::Vsync events since boot), the same numbering as run.lua's frames.
--
-- Only the game is traced: events count from the EXE's entry point, reached with the EXE's own code in RAM
-- (EXE_CHECK), until a reset; the BIOS's stores before it are only counted (boot_writes in the stats).
--
-- The written value is not passed to a breakpoint invoker; it is read from the store instruction at pc (the
-- breakpoint fires before the store completes, pc = the store): rt's register, masked to the width. A store whose
-- effective address (base + offset) does not match the breakpoint's address is reported with value "?".
local ffi = require('ffi')
local C = ffi.load('PCSX')
local mem = PCSX.getMemPtr()
local rom = PCSX.getRomPtr()
local regs = PCSX.getRegisters()

local out_dir = assert(os.getenv('DCB_SPU_TRACE_OUT'), 'DCB_SPU_TRACE_OUT not set')
local spec_path = os.getenv('DCB_SPU_TRACE_SPEC')
local spec = spec_path and dofile(spec_path) or {}

local SPU_BASE, SPU_SIZE = 0x1F801C00, 0x200
local DMA4_BASE, DMA4_SIZE = 0x1F8010C0, 0x10   -- MADR, BCR, CHCR (+ the unused 0x1F8010CC)
local EXE_ENTRY = 0x80056270                     -- SLUS_013.28's pc0 (__SN_ENTRY_POINT)
-- Words of SLUS_013.28 (in LIBSND: SsStart, SsSeqCalledTbyT) that must be in RAM: the EXE is resident.
local EXE_CHECK = { { 0x8004EED8, 0x0C013B28 }, { 0x8004EFB8, 0x8C42860C } }

local tick = 0
local started = false                -- the game's EXE is running (events are recorded)
local lines = {}
local nwrites, nboot, ndma, ncalls, nbad = 0, 0, 0, 0, 0
local dma_madr, dma_bcr = 0, 0       -- the last values written to DMA4's MADR and BCR
local data_file = assert(io.open(out_dir .. '/data.bin', 'wb'))
local data_offset = 0
local counts = {}                    -- name -> calls

local function ram_word(addr) return ffi.cast('uint32_t*', mem + bit.band(addr, 0x1FFFFC))[0] end

-- A word of code at a CPU address (RAM or the BIOS ROM).
local function code_word(addr)
    local phys = bit.band(addr, 0x1FFFFFFF)
    if phys >= 0x1FC00000 and phys < 0x1FC80000 then
        return ffi.cast('uint32_t*', rom + (phys - 0x1FC00000))[0]
    end
    return ram_word(phys)
end

local WIDTH_MASK = { [1] = 0xFF, [2] = 0xFFFF }

-- The value of the store at pc to `address`, or nil when the instruction there is not that store.
local function stored_value(pc, address, width)
    local ins = code_word(pc)
    local op = bit.rshift(ins, 26)
    if op ~= 0x28 and op ~= 0x29 and op ~= 0x2B then return nil end  -- sb, sh, sw
    local base = bit.band(bit.rshift(ins, 21), 31)
    local rt = bit.band(bit.rshift(ins, 16), 31)
    local imm = bit.band(ins, 0xFFFF)
    if imm >= 0x8000 then imm = imm - 0x10000 end
    local ea = bit.band(regs.GPR.r[base] + imm, 0x1FFFFFFF)
    if ea ~= bit.band(address, 0x1FFFFFFF) then return nil end
    local v = regs.GPR.r[rt]
    if width == 4 then return v end
    return bit.band(v, WIDTH_MASK[width])
end

local function hex32(v) return string.format('%08x', v) end

-- `len` bytes of RAM at `addr` into data.bin; returns their offset there.
local function save_ram(addr, len)
    local src = bit.band(addr, 0x1FFFFF)
    if src + len > 0x200000 then len = 0x200000 - src end
    data_file:write(ffi.string(mem + src, len))
    local at = data_offset
    data_offset = data_offset + len
    return at, len
end

-- Every store into the SPU's or DMA4's registers.
DCB_SPU_TRACE_WRITE = ffi.cast('bool (*)(uint32_t, unsigned, const char *)', function(address, width, cause)
    if not started then
        nboot = nboot + 1
        return true
    end
    local pc = regs.pc
    local v = stored_value(pc, address, width)
    nwrites = nwrites + 1
    local vs
    if v == nil then
        nbad = nbad + 1
        vs = '?'
    else
        vs = string.format('%x', v)
    end
    lines[#lines + 1] = string.format('w %d %08x %d %s %08x', tick, address, width, vs, pc)
    local reg = bit.band(address, 0x1FFFFFFF)
    if v ~= nil and reg >= DMA4_BASE and reg < DMA4_BASE + DMA4_SIZE then
        local r = reg - DMA4_BASE
        if r == 0 then
            dma_madr = v
        elseif r == 4 then
            dma_bcr = v
        elseif r == 8 and bit.band(v, 0x01000000) ~= 0 and bit.band(v, 1) == 1 then
            -- CHCR start, RAM to device (bit 0): the block mode's length is BCR's block size (words) x block count.
            -- The source is read now, at the store that starts the transfer.
            local words = bit.band(dma_bcr, 0xFFFF)
            local blocks = bit.rshift(dma_bcr, 16)
            if blocks == 0 then blocks = 1 end
            local at, len = save_ram(dma_madr, words * blocks * 4)
            lines[#lines + 1] = string.format('d %d %08x %08x %08x %d %d', tick, dma_madr, dma_bcr, v, at, len)
            ndma = ndma + 1
        end
    end
    return true
end)

-- Logged and counted function entries.
local call_info = {}   -- addr -> { name, log, dump }
for _, f in ipairs(spec.calls or {}) do call_info[f.addr] = { name = f.name, log = true, dump = f.dump } end
for _, f in ipairs(spec.counts or {}) do
    if not call_info[f.addr] then call_info[f.addr] = { name = f.name, log = false } end
end
DCB_SPU_TRACE_CALL = ffi.cast('bool (*)(uint32_t, unsigned, const char *)', function(address, width, cause)
    local info = call_info[address]
    if info and started then
        ncalls = ncalls + 1
        counts[info.name] = (counts[info.name] or 0) + 1
        if info.log then
            local g = regs.GPR.n
            local sp = g.sp
            local extra = ''
            if info.dump == 'chunk' and g.a0 ~= 0 then
                local size = ram_word(g.a0 - 4)
                if size > 0 and size < 0x100000 then
                    local at, len = save_ram(g.a0, size)
                    extra = string.format(' chunk=%d:%d', at, len)
                end
            elseif info.dump == 'cycles' then
                -- the CPU's cycle count (33,868,800 a second: 768 per SPU sample), for the samples between two ticks
                extra = string.format(' cycles=%.0f', tonumber(PCSX.getCPUCycles()))
            elseif type(info.dump) == 'number' and g.a0 ~= 0 then
                local src = bit.band(g.a0, 0x1FFFFF)
                local bytes = ffi.string(mem + src, info.dump)
                extra = ' mem=' .. bytes:gsub('.', function(c) return string.format('%02x', c:byte()) end)
            end
            lines[#lines + 1] = string.format('c %d %s %s %s %s %s %s %s %s %s %s%s', tick, info.name, hex32(g.a0),
                hex32(g.a1), hex32(g.a2), hex32(g.a3), hex32(ram_word(sp + 0x10)), hex32(ram_word(sp + 0x14)),
                hex32(ram_word(sp + 0x18)), hex32(ram_word(sp + 0x1C)), hex32(g.ra), extra)
        end
    end
    return true
end)

-- The game's entry point, with the EXE's code resident: start recording.
DCB_SPU_TRACE_ENTRY = ffi.cast('bool (*)(uint32_t, unsigned, const char *)', function(address, width, cause)
    if started then return true end
    for _, w in ipairs(EXE_CHECK) do
        if ram_word(w[1]) ~= w[2] then return true end
    end
    started = true
    lines[#lines + 1] = string.format('m %d exe_start', tick)
    return true
end)

DCB_SPU_TRACE_BPS = {
    C.addBreakpoint(SPU_BASE, 'Write', SPU_SIZE, 'spu', DCB_SPU_TRACE_WRITE, 'spu_trace'),
    C.addBreakpoint(DMA4_BASE, 'Write', DMA4_SIZE, 'dma4', DCB_SPU_TRACE_WRITE, 'spu_trace'),
    C.addBreakpoint(EXE_ENTRY, 'Exec', 4, 'entry', DCB_SPU_TRACE_ENTRY, 'spu_trace'),
}
local ncallbps = 0
for addr in pairs(call_info) do
    DCB_SPU_TRACE_BPS[#DCB_SPU_TRACE_BPS + 1] = C.addBreakpoint(addr, 'Exec', 4, 'call', DCB_SPU_TRACE_CALL, 'spu_trace')
    ncallbps = ncallbps + 1
end

-- The tick: one per vsync, counted like run.lua's frames (both listeners run on every vsync).
DCB_SPU_TRACE_LISTENER = PCSX.Events.createEventListener('GPU::Vsync', function() tick = tick + 1 end)
-- A reset (a script's reset step) reboots through the BIOS: stop until the EXE starts again.
DCB_SPU_TRACE_RESET = PCSX.Events.createEventListener('ExecutionFlow::Reset', function()
    if started then lines[#lines + 1] = string.format('m %d reset', tick) end
    started = false
end)

local function write_out()
    data_file:close()
    local f = assert(io.open(out_dir .. '/raw.txt', 'wb'))
    f:write(string.format('stats ticks=%d writes=%d boot_writes=%d dma=%d data_bytes=%d calls=%d unknown_values=%d\n',
        tick, nwrites, nboot, ndma, data_offset, ncalls, nbad))
    f:write(table.concat(lines, '\n'), '\n')
    f:close()
    local names = {}
    for name in pairs(counts) do names[#names + 1] = name end
    table.sort(names)
    f = assert(io.open(out_dir .. '/counts.txt', 'wb'))
    for _, name in ipairs(names) do f:write(string.format('%s %d\n', name, counts[name])) end
    f:close()
end

-- The runner ends with PCSX.quit: write the trace first.
local quit = PCSX.quit
PCSX.quit = function(code)
    write_out()
    quit(code)
end
print(string.format('spu_trace: SPU and DMA4 write breakpoints, %d function breakpoints', ncallbps))
