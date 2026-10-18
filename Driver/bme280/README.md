# Bosch BME280 SensorAPI (P5a)

Source: https://github.com/boschsensortec/BME280_SensorAPI
Pinned commit: `c90d419492e26dd95586598a794e65eb2760753a`.
Source headers identify v3.5.1 (2020-12-17). The three upstream source/header
files and LICENSE are copied unchanged. License: BSD-3-Clause; retain LICENSE
and source notices, including the binary redistribution notice requirement.

Keil defines `BME280_64BIT_ENABLE` for all C translation units. ARMCC5 C99
provides stdint.h, INT64_C and int64_t; no COINES/Arduino dependency. P5a's
adapter rejects DOUBLE/32BIT modes at compile time. In this pinned version:

- temperature is signed 0.01 C: copied directly;
- humidity is unsigned 1/1024 %RH: `(H * 100 + 512) / 1024` to 0.01 %RH;
- pressure is unsigned 0.01 Pa: `(P + 50) / 100` to Pa, **not Q24.8**.

Bounds before conversion: T -4000..8500, H 0..102400, P 3000000..11000000.
Thus H*100+512 <= 10240512 and P+50 <= 11000050 fit uint32_t.
The upstream compensators clamp to sensor limits; these checks do not certify
accuracy or detect every corrupt calibration value. Init additionally rejects
dig_p1=0; the adapter rejects skipped ADC sentinels.

The uploaded Resources/BME280_I2C.ino was reviewed, not ported: it lacks checked
ID/read lengths/IO results, uses signed H1/H3, does not sign-extend H4/H5
reliably, and writes config after starting normal mode. Bosch handles chip ID,
trim parsing/sign extension and compensation. P5a uses x1 forced T/P/H and
checks measuring/im_update clear and sleep mode before reading sample data.

HAL adaptation lives in App/env/env_app.c. In this repository's F1 HAL,
Mem_Read >=3-byte tails can wait on BTF with PRIMASK set. Reads are split into
1/2-byte HAL transactions to avoid that path, including calibration/settings.
Forced-mode data is read only after conversion has finished and returned to
sleep, so separate reads still belong to the same stable conversion. All
polling waits retain interrupts; the HAL's brief 1/2-byte register sequences
still mask IRQs. Timeout is 5 ms per transaction; a BUSY precheck avoids the
HAL's fixed 25 ms BUSY wait (precheck allows up to 5 ms for the previous STOP
to finish, with interrupts enabled). No HAL source changes or GPIO bus recovery.

Bosch bme280_cal_meas_delay returns **microseconds**, despite a stale internal
comment saying milliseconds: constants are 1250/2300/575; x1 T/P/H is 9300 us.
The task delay callback rounds up to ticks plus one phase tick, giving at least
the requested delay without busy-waiting. Init failure remains invalid until
MCU reset; subsequent sample errors invalidate while preserving last-good
values/tick, with the next ordinary acquisition attempted 500 ms later.
