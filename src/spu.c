/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 15, 2023
 */

#include "emulator.h"

#define SPU_NPHASES 16

void update_spu_sound_amp(struct emulator *gameboy) {
    struct gameboy_spu *spu = &gameboy->spu;
    unsigned max_amplitude = 15; // each sound generates 4bit unsigned values
    unsigned scaling;

    max_amplitude *= 8; // can be amplified up to 8 times
    max_amplitude *= 4; // sum up 4 sounds

    scaling = 0x7FFF / max_amplitude; // linear scaling to saturate the output at max amplitude

    for (unsigned sound = 0; sound < 4; sound++) {
        for (unsigned channel = 0; channel < 2; channel++) {
            bool enabled = spu->sound_mux & (1 << (sound + channel * 4));
            int16_t amp;

            if (enabled) {
                amp = 1;
                amp += (spu->output_level >> (channel * 4)) & 7;
                amp *= scaling;
            } else {
                amp = 0;
            }

            spu->sound_amp[sound][channel] = amp;
        }
    }
}

static void reload_spu_frequency(struct spu_divider *f) {
    f->counter = 2 * (0x800U - f->offset);
}

static void reload_spu_lfsr_counter(struct spu_nr4 *nr4) {
    uint8_t div = nr4->lfsr_configuration & 7;
    uint8_t shift = (nr4->lfsr_configuration >> 4) + 1;

    if (div == 0) {
        nr4->counter = 4;
    } else {
        nr4->counter = 8 * div;
    }

    nr4->counter <<= shift;
}

void reload_spu_sweep(struct spu_sweep *f, uint8_t configuration) {
    f->shift = configuration & 0x7;
    f->subtract = (configuration >> 3) & 1;
    f->time = (configuration >> 4) & 0x7;

    f->counter = 0x8000 * f->time;
}

void reset_spu(struct emulator *gameboy) {
    struct gameboy_spu *spu = &gameboy->spu;

    spu->enable = true;
    spu->output_level = 0;
    spu->sound_mux = 0;

    update_spu_sound_amp(gameboy);

    // NR1 reset
    spu->nr1.running = false;
    spu->nr1.duration.enable = false;
    spu->nr1.wave.duty_cycle = 0;
    spu->nr1.envelope_configuration = 0;
    spu->nr1.sweep.divider.offset = 0;

    reload_spu_frequency(&spu->nr1.sweep.divider);
    reload_spu_sweep(&spu->nr1.sweep, 0);

    // NR2 reset
    spu->nr2.running = false;
    spu->nr2.duration.enable = false;
    spu->nr2.wave.duty_cycle = 0;
    spu->nr2.envelope_configuration = 0;
    spu->nr2.divider.offset = 0;

    reload_spu_frequency(&spu->nr2.divider);

    // NR3 reset
    spu->nr3.enable = false;
    spu->nr3.running = false;
    spu->nr3.duration.enable = false;
    spu->nr3.volume_shift = 0;
    spu->nr3.divider.offset = 0;
    spu->nr3.t1 = 0;
    spu->nr3.index = 0;
    spu->nr3.divider.offset = 0;

    reload_spu_frequency(&spu->nr3.divider);

    // NR4 reset
    spu->nr4.running = false;
    spu->nr4.duration.enable = false;
    spu->nr4.envelope_configuration = 0;
    spu->nr4.lfsr_configuration = 0;
    spu->nr4.lfsr = 0x7FFF;
}

void reload_spu_duration(struct spu_duration *d, unsigned duration_max, uint8_t t1) {
    d->counter = (duration_max + 1 - t1) * 0x4000U;
}

// run the duration counter if it's enabled ; returns true, if the counter reached zero and the channel should be disabled
static bool update_spu_duration(struct spu_duration *d, unsigned duration_max, unsigned cycles) {
    bool elapsed = false;

    if (!d->enable) {
        return false;
    }

    while (cycles) {
        if (d->counter > cycles) {
            d->counter -= cycles;
            cycles = 0;
        } else {
            elapsed = true;
            cycles -= d->counter;

            reload_spu_duration(d, duration_max, 0);
        }
    }

    return elapsed;
}

// update the frequency counter ; return the number of times it ran out
static unsigned update_spu_frequency(struct spu_divider *f, unsigned cycles) {
    unsigned count = 0;

    while (cycles) {
        if (f->counter > cycles) {
            f->counter -= cycles;
            cycles = 0;
        } else {
            count++;
            cycles -= f->counter;

            reload_spu_frequency(f);
        }
    }

    return count;
}

// update the sweep function and the frequency counter ; return the number of times it ran out
static unsigned update_spu_sweep(struct spu_sweep *s, unsigned cycles, bool *disable) {
    unsigned count = 0;
    *disable = false;

    if (s->time == 0) {
        return update_spu_frequency(&s->divider, cycles); // sweep is disabled
    }

    // need to step the sweep function and the frequency function alongside since the frequency changes with the sweep
    while (cycles) {
        unsigned to_run = cycles;

        if (s->counter < to_run) {
            to_run = s->counter;
        }

        if (s->divider.counter < to_run) {
            to_run = s->divider.counter;
        }

        s->counter -= to_run;
        if (s->counter == 0) {
            uint16_t delta = s->divider.offset >> s->shift; // sweep step elapsed

            if (s->subtract) {
                if (s->shift != 0 && delta <= s->divider.offset) {
                    s->divider.offset -= delta;
                }
            } else {
                uint32_t o = s->divider.offset;

                o += delta;

                if (o > 0x7FF) {
                    // if the addition overflows, then the sound is disabled
                    *disable = true;
                    break;
                }

                s->divider.offset = o;
            }

            s->counter = 0x8000 * s->time; // reload counter
        }

        count += update_spu_frequency(&s->divider, to_run);
        cycles -= to_run;
    }

    return count;
}

static uint8_t spu_next_wave_sample(struct spu_rectangle_wave *wave, unsigned phase_steps) {
    static const uint8_t waveforms[4][SPU_NPHASES / 2] = {
        {1, 0, 0, 0, 0, 0, 0, 0}, // 1/8
        {1, 1, 0, 0, 0, 0, 0, 0}, // 1/4
        {1, 1, 1, 1, 0, 0, 0, 0}, // 1/2
        {1, 1, 1, 1, 1, 1, 0, 0}, // 3/4
    };

    wave->phase = (wave->phase + phase_steps) % SPU_NPHASES;

    return waveforms[wave->duty_cycle][wave->phase / 2];
}

static void spu_envelope_reload_counter(struct spu_envelope *e) {
    e->counter = e->step_duration * 0x10000;
}

// reload the envelope configuration from the register value
static void init_spu_envelope(struct spu_envelope *e, uint8_t configuration) {
    e->value = configuration >> 4;
    e->increment = (configuration & 8);
    e->step_duration = configuration & 7;

    spu_envelope_reload_counter(e);
}

static bool spu_envelope_active(struct spu_envelope *e) {
    // the envelope is stopped if the value is 0 and we're set to decrement
    return e->value != 0 || e->increment;
}

// run the envelope if it's enabled ; returns true, if the envelope reached an inactive state and the channel should be disabled
static bool spu_envelope_update(struct spu_envelope *e, unsigned cycles) {
    if (e->step_duration != 0) {
        while (cycles) {
            if (e->counter > cycles) {
                e->counter -= cycles;
                cycles = 0;
            } else {
                // step counter elapsed ; apply envelope function
                cycles -= e->counter;

                if (e->increment) {
                        if (e->value < 0xf) {
                            e->value++;
                        }
                } else {
                        if (e->value > 0) {
                            e->value--;
                        }
                }

                spu_envelope_reload_counter(e);
            }
        }
    }

    return !spu_envelope_active(e);
}

static uint8_t spu_next_nr1_sample(struct emulator *gameboy, unsigned cycles) {
    struct gameboy_spu *spu = &gameboy->spu;
    uint8_t sample;
    unsigned sound_cycles;
    bool disable;

    // the duration counter runs even if the sound itself is not running
    if (update_spu_duration(&spu->nr1.duration, GB_SPU_NR1_T1_MAX, cycles)) {
        spu->nr1.running = false;
    }

    if (!spu->nr1.running) {
        return 0;
    }

    if (spu_envelope_update(&spu->nr1.envelope, cycles)) {
        spu->nr1.running = false;
    }

    if (!spu->nr1.running) {
        return 0;
    }

    sound_cycles = update_spu_sweep(&spu->nr1.sweep, cycles, &disable);
    if (disable) {
        spu->nr1.running = false;
        return 0;
    }

    sample = spu_next_wave_sample(&spu->nr1.wave, sound_cycles);
    sample *= spu->nr1.envelope.value;

    return sample;
}

static uint8_t spu_next_nr2_sample(struct emulator *gameboy, unsigned cycles) {
    struct gameboy_spu *spu = &gameboy->spu;
    uint8_t sample;
    unsigned sound_cycles;

    // the duration counter runs even if the sound itself is not running
    if (update_spu_duration(&spu->nr2.duration, GB_SPU_NR2_T1_MAX, cycles)) {
        spu->nr2.running = false;
    }

    if (!spu->nr2.running) {
        return 0;
    }

    if (spu_envelope_update(&spu->nr2.envelope, cycles)) {
        spu->nr2.running = false;
    }

    if (!spu->nr2.running) {
        return 0;
    }

    sound_cycles = update_spu_frequency(&spu->nr2.divider, cycles);

    sample = spu_next_wave_sample(&spu->nr2.wave, sound_cycles);
    sample *= spu->nr2.envelope.value;

    return sample;
}

static uint8_t spu_next_nr3_sample(struct emulator *gameboy, unsigned cycles) {
    struct gameboy_spu *spu = &gameboy->spu;
    uint8_t sample;
    unsigned sound_cycles;

    // the duration counter runs even if the sound itself is not running */
    if (update_spu_duration(&spu->nr3.duration, GB_SPU_NR3_T1_MAX, cycles)) {
        spu->nr3.running = false;
    }

    if (!spu->nr3.running) {
        return 0;
    }

    sound_cycles = update_spu_frequency(&spu->nr3.divider, cycles);

    spu->nr3.index = (spu->nr3.index + sound_cycles) % (GB_NR3_RAM_SIZE * 2);

    if (spu->nr3.volume_shift == 0) {
        // sound is muted
        return 0;
    }

    // two samples are packed per byte
    sample = spu->nr3.ram[spu->nr3.index / 2];

    if (spu->nr3.index & 1) {
        sample &= 0xF;
    } else {
        sample >>= 4;
    }

    return sample >> (spu->nr3.volume_shift - 1);
}

static void spu_lfsr_step(struct spu_nr4 *nr4) {
    // true, if the lfsr only uses 7 bits for the effective register period
    bool period_7bits = nr4->lfsr_configuration & 0x8;
    uint16_t shifted;
    uint16_t carry;

    shifted = nr4->lfsr >> 1;
    carry = (nr4->lfsr ^ shifted) & 1;

    nr4->lfsr = shifted;
    nr4->lfsr |= carry << 14;

    if (period_7bits) {
        // carry is also copied to bit 6
        nr4->lfsr &= ~(1U << 6);
        nr4->lfsr |= carry << 6;
    }
}

static uint8_t spu_next_nr4_sample(struct emulator *gameboy, unsigned cycles) {
    struct gameboy_spu *spu = &gameboy->spu;
    uint8_t sample;

    // the duration counter runs even if the sound itself is not running
    if (update_spu_duration(&spu->nr4.duration, GB_SPU_NR4_T1_MAX, cycles)) {
        spu->nr4.running = false;
    }

    if (!spu->nr4.running) {
        return 0;
    }

    if (spu_envelope_update(&spu->nr4.envelope, cycles)) {
        spu->nr4.running = false;
    }

    if (!spu->nr4.running) {
        return 0;
    }

    while (cycles) {
        if (spu->nr4.counter > cycles) {
            spu->nr4.counter -= cycles;
            cycles = 0;
        } else {
            cycles -= spu->nr4.counter;
            reload_spu_lfsr_counter(&spu->nr4);
            spu_lfsr_step(&spu->nr4);
        }
    }

    // sample is 0 if the LFSR's LSB is 0, otherwise it's the envelope's value
    sample = spu->nr4.lfsr & 1;
    sample *= spu->nr4.envelope.value;

    return sample;
}

// send a pair of left / right samples to the ui
static void send_spu_sample_to_ui(struct emulator *gameboy, int16_t sample_left, int16_t sample_right) {
    struct gameboy_spu *spu = &gameboy->spu;
    struct spu_sample_buffer *buffer;

    buffer = &spu->buffers[spu->buffer_index];

    if (spu->sample_index == 0) {
        sem_wait(&buffer->free); // wait unitl buffer is free, if necessary
    }

    buffer->samples[spu->sample_index][0] = sample_left;
    buffer->samples[spu->sample_index][1] = sample_right;

    spu->sample_index++;

    if (spu->sample_index == GB_SPU_SAMPLE_BUFFER_LENGTH) {
        sem_post(&buffer->ready);

        spu->buffer_index = (spu->buffer_index + 1)% GB_SPU_SAMPLE_BUFFER_COUNT;
        spu->sample_index = 0;
    }
}

void sync_spu(struct emulator *gameboy) {
    struct gameboy_spu *spu = &gameboy->spu;
    int32_t elapsed = resync_sync(gameboy, GB_SYNC_SPU);
    int32_t period = spu->sample_period;
    int32_t nsamples;
    int32_t next_sync;

    elapsed += period;

    nsamples = elapsed / GB_SPU_SAMPLE_RATE_DIVISOR;

    while (nsamples--) {
        int32_t next_sample_delay = GB_SPU_SAMPLE_RATE_DIVISOR - period;
        unsigned sound;
        int16_t sound_samples[4];
        int16_t sample_l = 0;
        int16_t sample_r = 0;

        sound_samples[0] = spu_next_nr1_sample(gameboy, next_sample_delay);
        sound_samples[1] = spu_next_nr2_sample(gameboy, next_sample_delay);
        sound_samples[2] = spu_next_nr3_sample(gameboy, next_sample_delay);
        sound_samples[3] = spu_next_nr4_sample(gameboy, next_sample_delay);

        for (sound = 0; sound < 4; sound++) {
            sample_l += sound_samples[sound] * spu->sound_amp[sound][0];
            sample_r += sound_samples[sound] * spu->sound_amp[sound][1];
        }

        send_spu_sample_to_ui(gameboy, sample_l, sample_r);

        period = 0;
    }

    period = elapsed % GB_SPU_SAMPLE_RATE_DIVISOR;

    // advance the SPU state even if we don't want the sample yet in order to have the correct value for the running flags
    spu_next_nr1_sample(gameboy, period);
    spu_next_nr2_sample(gameboy, period);
    spu_next_nr3_sample(gameboy, period);
    spu_next_nr4_sample(gameboy, period);

    spu->sample_period = period;

    // schedule a sync to fill the current buffer
    next_sync = (GB_SPU_SAMPLE_BUFFER_LENGTH - spu->sample_index) * GB_SPU_SAMPLE_RATE_DIVISOR;
    next_sync -= period;

    sync_next(gameboy, GB_SYNC_SPU, next_sync);
}

void start_spu_nr1(struct emulator *gameboy) {
    struct gameboy_spu *spu = &gameboy->spu;

    spu->nr1.wave.phase = 0;

    reload_spu_frequency(&spu->nr1.sweep.divider);
    init_spu_envelope(&spu->nr1.envelope, spu->nr1.envelope_configuration);

    spu->nr1.running = spu_envelope_active(&spu->nr1.envelope);
}

void start_spu_nr2(struct emulator *gameboy) {
    struct gameboy_spu *spu = &gameboy->spu;

    spu->nr2.wave.phase = 0;

    reload_spu_frequency(&spu->nr2.divider);
    init_spu_envelope(&spu->nr2.envelope, spu->nr2.envelope_configuration);

    spu->nr2.running = spu_envelope_active(&spu->nr2.envelope);
}

void start_spu_nr3(struct emulator *gameboy) {
    struct gameboy_spu *spu = &gameboy->spu;

    if (!spu->nr3.enable) {
        return;
    }

    spu->nr3.index = 0;
    spu->nr3.running = true;

    reload_spu_frequency(&spu->nr3.divider);
}

void start_spu_nr4(struct emulator *gameboy) {
    struct gameboy_spu *spu = &gameboy->spu;

    init_spu_envelope(&spu->nr4.envelope, spu->nr4.envelope_configuration);
    reload_spu_lfsr_counter(&spu->nr4);

    spu->nr4.running = true;
}
