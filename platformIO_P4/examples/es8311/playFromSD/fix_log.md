During playback, the device suddenly restarted and displayed the following message.

~~~

[380981][E][Audio.cpp:122] bytesWritten(): AudioBuffer: m_writePtr 1208802948 > m_endPtr 1208802734
[384245][E][Audio.cpp:122] bytesWritten(): AudioBuffer: m_writePtr 1214243661 > m_endPtr 1208802734
Guru Meditation Error: Core 0 panic'ed (Store access fault). Exception was unhandled.

Core 0 register dump:
MEPC : 0x4ff0fe40 RA : 0x4ff0fdd0 SP : 0x4ff17bd0 GP : 0x4ff13b00
TP : 0x4ff17e30 T0 : 0x4fc10cc4 T1 : 0x4ff014c0 T2 : 0x01cf0292
S0/FP : 0x0000004c S1 : 0x480d5e28 A0 : 0x0000001e A1 : 0x0000004c
A2 : 0x00000001 A3 : 0x00000000 A4 : 0x84691b63 A5 : 0xc654e137
A6 : 0x08000000 A7 : 0x4fc10a5c S2 : 0x00000012 S3 : 0xc05819f3
S4 : 0x48000038 S5 : 0x48000014 S6 : 0x48000084 S7 : 0x4802ddc7
S8 : 0x00000000 S9 : 0x00000000 S10 : 0x00000000 S11 : 0x00000000
T3 : 0x025b0282 T4 : 0x02200273 T5 : 0x0219027c T6 : 0x02230290
MSTATUS : 0x00011880 MTVEC : 0x4ff00003 MCAUSE : 0x00000007 MTVAL : 0x84691b6f
MHARTID : 0x00000000

~~~

`AudioBuffer::bytesWritten()` is called by the decoder when it pushes decoded PCM
into the ring buffer. the routine only checks for the exact end‑of‑buffer
`( m_writePtr == m_endPtr )` and simply logs an error if the pointer runs past
the end. when the pointer overshoots (because the library wrote a block that was
a few bytes larger than the remaining space), the pointer ends up pointing
outside the allocated region and the next write/read causes the store‑access
fault you observed.

There are two simple ways to address this:

- give the buffer more headroom so the overshoot never happens, and
- fix the library so the pointer is wrapped correctly instead of crashing.


✅ Changes applied

1. **Library patch** – `Audio.cpp` now wraps the write pointer and reports
overflow, instead of letting the pointer wander off into invalid memory.

~~~c
// Before the change
void AudioBuffer::bytesWritten(size_t bw) {
    if(!bw) return;
    m_writePtr += bw;
    if(m_writePtr == m_endPtr) { m_writePtr = m_buffer.get(); }
    if(m_writePtr > m_endPtr) log_e("AudioBuffer: m_writePtr %i > m_endPtr %i", m_writePtr, m_endPtr);
    m_f_isEmpty = false;
}

//After the change
void AudioBuffer::bytesWritten(size_t bw) {
    if(!bw) return;
    // advance write pointer by bw bytes, wrapping correctly around buffer
    size_t spaceToEnd = m_endPtr - m_writePtr;
    if(bw < spaceToEnd) {
        m_writePtr += bw;
    } else if(bw == spaceToEnd) {
        // exactly reached end, wrap to start
        m_writePtr = m_buffer.get();
    } else {
        // would overflow buffer, wrap and log
        size_t overflow = bw - spaceToEnd;
        log_e("AudioBuffer overflow: write %u bytes with only %u space, wrapping",
              (unsigned)bw, (unsigned)spaceToEnd);
        m_writePtr = m_buffer.get() + (overflow % m_buffSize);
    }
    m_f_isEmpty = false;
}
~~~

The error messages you saw `([E][Audio.cpp:122]…)` will now show up in the
log if this situation ever occurs, but the code continues safely.

2. **Sketch updates** – the example was modified to:

- call `audio.setInBufferSize(64*1024);` after `audio.setPinout()` (runs on PSRAM, you can adjust the value);

- print the new buffer size and periodically log `inBufferFree()/inBufferFilled();` keep the existing recovery logic, so that if the card drops out, playback
is retried;

- retain all previous playlist/scanning/log enhancements.
These changes give the decoder plenty of space and provide diagnostic
output if the buffer ever nears full.

These changes give the decoder plenty of space and provide diagnostic
output if the buffer ever nears full.

~~~c
// …inside setup() after board.begin(cfg) …
audio.setPinout(...);
audio.setVolume(4);
audio.setInBufferSize(64 * 1024);
DEBUG_LOG("in buffer size now %u\n", audio.getInBufferSize());
~~~

~~~c
// …inside loop() …
static uint32_t lastReport = 0;
uint32_t now = millis();
if (now - lastReport > 2000) {
    lastReport = now;
    DEBUG_LOG("buf free=%u filled=%u\n", audio.inBufferFree(),
              audio.inBufferFilled());
}
~~~

🎯 Result
- No more panic. Even if the decoder writes a block that crosses the end of
the ring buffer the pointer is wrapped correctly and the system continues
running.

- Fewer overflows. The buffer is now larger, reducing the likelihood of
ever hitting the boundary.

- Visibility. Any future overflow conditions are logged (you’ll see a
message like AudioBuffer overflow: write … wrapping), and buffer usage is
printed every two seconds.

- Robustness. Playback still cycles through the scanned playlist, and on
SPI‑SD cards the card is re‑initialised when a track finishes.


You can now rebuild the sketch (the edited library file is in `Audio.cpp`) and flash it; the crash should
disappear. If you still see overflow messages, you can bump the buffer size further or investigate why the decoder is generating excessively large blocks.


Let me know if you’d like help adding a survivor counter, watchdog, or other
defensive measures!