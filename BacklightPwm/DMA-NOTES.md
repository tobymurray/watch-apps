# Timer + DMA backlight PWM: register plan

Everything below is from ST's own headers, not inferred. Sources:

- `STMicroelectronics/cmsis_device_u5`, `Include/stm32u5a5xx.h` (bases, register
  offsets, bit positions; offsets cross-checked against the header's own
  "Address offset:" comments rather than a struct walk).
- `STMicroelectronics/stm32u5xx_hal_driver`, `Inc/stm32u5xx_hal_dma.h` (GPDMA
  request numbers).

Two claims this incidentally settles, both previously hedged in the
investigation:

- `GPIOF` is at `0x42021400` (`AHB2PERIPH_BASE_NS + 0x1400`, and
  `AHB2PERIPH_BASE_NS = 0x42020000`). Matches what BacklightProbe measured.
- `RCC` at `0x46020C00` with `AHB1ENR` at `+0x88` and `APB1ENR1` at `+0x9C`, so
  the existing 64-word RCC sweep **does** cover the clock-enable registers. That
  was carried as UNVERIFIED because RM0456 was not available.

## The pieces

| What | Address | Notes |
| --- | --- | --- |
| `GPIOF BSRR` | `0x42021418` | The destination. Bit 3 set = off, bit 19 (BR3) = on |
| `RCC AHB1ENR` | `0x46020C88` | bit 0 `GPDMA1EN` |
| `RCC APB1ENR1` | `0x46020C9C` | bit 4 `TIM6EN`, bit 5 `TIM7EN` |
| `TIM6` | `0x40001000` | `CR1 +0x00` (CEN bit 0), `DIER +0x0C` (UDE bit 8), `EGR +0x14` (UG bit 0), `PSC +0x28`, `ARR +0x2C` |
| `TIM7` | `0x40001400` | same layout |
| `GPDMA1` | `0x40020000` | channel N at `+0x50 + N*0x80` |

GPDMA channel registers, relative to the channel base:

| Reg | Offset | Use |
| --- | --- | --- |
| `CLBAR` | `+0x00` | linked-list base; unused in block-repeat mode |
| `CFCR` | `+0x0C` | flag clear (`TCF` bit 8) |
| `CSR` | `+0x10` | status (`IDLEF` bit 0) |
| `CCR` | `+0x14` | `EN` bit 0, `RESET` bit 1, `SUSP` bit 2 |
| `CTR1` | `+0x40` | `SDW_LOG2[1:0]`, `SINC` bit 3, `DDW_LOG2[17:16]`, `DINC` bit 19 |
| `CTR2` | `+0x44` | `REQSEL[6:0]`, `SWREQ` bit 9, `DREQ` bit 10, `BREQ` bit 11 |
| `CBR1` | `+0x48` | `BNDT[15:0]` bytes, `BRC[26:16]` repeats, `BRSDEC` bit 30 |
| `CSAR` | `+0x4C` | source: the waveform buffer |
| `CDAR` | `+0x50` | destination: `GPIOF BSRR`, **fixed** |
| `CBR2` | `+0x58` | `BRSAO[15:0]` source offset applied per block repeat |
| `CLLR` | `+0x7C` | linked list; zero here |

Request numbers: `GPDMA1_REQUEST_TIM6_UP = 4`, `TIM7_UP = 5`.

## Why block-repeat rather than a linked list

The obvious way to make a GPDMA transfer circular is a linked-list node in RAM
that points back at itself. It works, and it means getting `CLBAR`, `CLLR`
update bits and a correctly aligned node right, blind, with no debugger.

Block-repeat mode does the same job with no list at all: one block of `BNDT`
bytes, repeated `BRC + 1` times, with `BRSAO` rewinding the source address to
the start of the buffer at each block boundary (`BRSDEC` set, so the offset is
subtracted). Fewer registers, no memory node, no alignment rule.

`BRC` is 11 bits, so a block repeats at most 2048 times and the transfer then
stops. At 128 words and a 25.6 kHz tick that is a 5 ms waveform running for
about 10 seconds, so the service re-arms roughly every 10 s. Re-arming costs one
register write and nothing else; it is not a busy loop.

## The waveform

A buffer of N words, each either `BR3` (`1 << 19`, light on) or `BS3` (`1 << 3`,
light off). Duty is how many words are `BR3`. With N = 128 the step is 0.78 %,
which is finer than the 12 levels the discrimination run showed to be
distinguishable.

The timer only produces an update event; nothing is routed to a pin, which is
the entire reason this works on PF3 at all. PF3 has no timer output, but it does
not need one: the DMA writes `BSRR` and `BSRR` does not care who wrote it.

## Choosing a timer and a channel

Both must be ones the kernel is not using.

- **Timer**: read `APB1ENR1`. A clear enable bit means the kernel is not using
  that timer. TIM6 and TIM7 are basic timers with no output pins, so they are
  the least likely to be wanted and the least disruptive to borrow.
- **DMA channel**: read each channel's `CCR`. `EN` clear and `CSR.IDLEF` set
  means idle. Pick the highest-numbered idle channel, on the theory that a
  kernel allocating channels will have started at zero.

If neither is available the app refuses to run and says so. That is a real
possible outcome and it is a finding rather than a failure.

## Safety, which matters more here than in BacklightPwm

BacklightPwm only ever wrote `BSRR`: write-only, self-clearing, nothing to
restore, and a crash leaves the pin in a state the kernel already uses.

This writes real configuration: a clock enable, timer registers, and a DMA
channel whose **destination address is under our control**. A wrong `CDAR` would
have the DMA engine writing into memory rather than a peripheral. That is the
first genuinely dangerous thing in this investigation, so:

1. Every register touched is read first and saved.
2. The channel is configured with `EN` clear, then **every written register is
   read back and compared against what was intended**. `CDAR` in particular must
   read exactly `0x42021418`. Any mismatch aborts and nothing is enabled.
3. Only after the readback passes is `CCR.EN` set.
4. Teardown disables the channel, waits for `IDLEF`, stops the timer, restores
   every saved register including the clock-enable bits, and hands the pin back
   to the kernel with a normal `RequestBacklightSet`.
5. A hard time cap, as in BacklightPwm.

The readback gate is the important one. It converts "a wrong address corrupts
memory" into "a wrong address stops the app", which is the difference between a
bug and an incident.
