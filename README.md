# cardputer_asv_mp3

Minimal MP3/WAV player for M5Stack Cardputer-ADV.

## Build

```bash
pio run -e cardputer-adv
```

## Upload / monitor

```bash
pio run -e cardputer-adv -t upload
pio device monitor -b 115200
```

## Tests (host)

```bash
pio test -e native
```

## Design

See [docs/superpowers/specs/](docs/superpowers/specs/) for the design specification and implementation plan.
