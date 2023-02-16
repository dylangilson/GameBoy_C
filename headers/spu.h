/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * Feburary 9, 2023
 */

// Sound Processing Unit

#ifndef SPU_H
#define SPU_H

#define GB_SPU_SAMPLE_RATE_DIVISOR 64 // sample SPU once every 64 CPU cycles
#define GB_SPU_SAMPLE_RATE_HZ (CPU_FREQUENCY_HZ / GB_SPU_SAMPLE_RATE_DIVISOR) // SPU sample rate
#define GB_SPU_SAMPLE_BUFFER_LENGTH 2048 // each sample contains 2048 frames ; each with two samples for left and right stereo channels
#define GB_SPU_SAMPLE_BUFFER_COUNT 2 // number of entries in sample buffer
#define GB_NR3_RAM_SIZE 16 // Sound 3 RAM size in bytes
#define GB_SPU_NR1_T1_MAX 0x3F // max duration of Sound 1
#define GB_SPU_NR2_T1_MAX 0x3F // max duration of Sound 2
#define GB_SPU_NR3_T1_MAX 0xFF // max duration of Sound 3
#define GB_SPU_NR4_T1_MAX 0x3F // max duration of Sound 4

struct spu_sample_buffer {
    int16_t samples[GB_SPU_SAMPLE_BUFFER_LENGTH][2]; // buffer of pairs of stereo samples
    sem_t free; // set to 1 when UI is done sending audio buffer ; set to 0 when the SPU starts adding new samples
    sem_t ready; // set to 1 when the SPU is adding a buffer and it can be sent by the UI ; set to 0 when UI starts sending samples
} spu_sample_buffer;

struct spu_duration {
    bool enable; // true if duration counter is enabled
    uint32_t counter; // keeps track of how much time has passed
} spu_duration;

struct spu_divider {
    uint16_t offset; // advance to next step every 0x800 - offset
    uint16_t counter; // counter to check for next step
} spu_divider;

struct spu_sweep {
    struct spu_divider divider; // frequency divider
    uint8_t shift; // frequency sweep amount
    bool subtract; // true if we subtract the offset ; false if we add the offset
    uint8_t time; // delay between sweep steps in 1/128th of a second ; value is set to 0 if this is disabled
    uint32_t counter; // counter to check for next sweep step
} spu_sweep;

struct spu_rectangle_wave {
    uint8_t phase; // current pahse within duty cycle
    uint8_t duty_cycle; // duty cycle : 1/8, 1/4, 1/2, 3/4
} spu_rectangle_wave;

struct spu_envelope {
    uint8_t step_duration; // duration of each addition / subtraction step in multiples of 65535 (1/64 seconds) ; value is 0 if envelope is stopped
    uint8_t value;
    bool increment; // true if we increment at each step ; false if we decrement at each step
    uint32_t counter; // counter to check for next step
} spu_envelope;

// Sound 1 : rectangluar wave with envelope and frequency sweep
struct spu_nr1 {
    bool running; // true if sound 1 is currently running
    struct spu_duration duration; // Sound 1's length counter
    struct spu_sweep sweep; // Sound 1's frequency divider and sweep function
    struct spu_rectangle_wave wave;
    uint8_t envelope_configuration;
    struct spu_envelope envelope;
} spu_nr1;

// Sound 2 : rectangular wave with envelope
struct spu_nr2 {
    bool running; // true if sound 2 is currently running
    struct spu_duration duration; // Sound 2's length counter
    struct spu_divider divider; // Sound 2's frequency divider
    struct spu_rectangle_wave wave;
    uint8_t envelope_configuration;
    struct spu_envelope envelope;
} spu_nr2;

// Sound 3 : user-defined waveform
struct spu_nr3 {
    bool enable; // true if sound 3 is enabled
    bool running; // true if sound 3 is currently running
    struct spu_duration duration; // Sound 3's length counter
    uint8_t t1; // register value for the length of counter
    struct spu_divider divider; // Sound 3's frequency divider
    uint8_t volume_shift; // 1 -> full volume ; 2 -> half volume ; 3 -> quarter volume ; 0 -> muted
    uint8_t ram[GB_NR3_RAM_SIZE]; // RAM of 32 4bit sound samples ; two samples per byte
    uint8_t index;    
} spu_nr3;

// Sound 4 : Linear Feedback Shift Register noise generation with envelope
struct spu_nr4 {
    bool running; // true if sound 4 is currently running
    struct spu_duration duration; // Sound 4's length counter
    uint8_t envelope_configuration;
    struct spu_envelope envelope;
    uint16_t lfsr;
    uint8_t lfsr_configuration; // LSFR configuration register (NR43)
    uint32_t counter; // counter to check for next LSFR shift
} spu_nr4;

struct gameboy_spu {
    bool enable; // master enable ; if false all SPU circuits are disabled
    uint8_t sample_period; // leftover cycless from previous sync if the number of cycles left is less than GB_SPU_SAMPLE_RATE_DIVISOR
    uint8_t output_level; // register NR50
    uint8_t sound_mux; // register NR51
    int16_t sound_amp[4][2]; // amplification factor for each sound for both stereo channels
    struct spu_nr1 nr1; // Sound 1 state
    struct spu_nr2 nr2; // Sound 2 state
    struct spu_nr3 nr3; // Sound 3 state
    struct spu_nr4 nr4; // Sound 4 state
    struct spu_sample_buffer buffers[GB_SPU_SAMPLE_BUFFER_COUNT];
    unsigned buffer_index; // buffer currently being filled
    unsigned sample_index; // position within current buffer
} gameboy_spu;

void reset_spu(struct emulator *gameboy);
void sync_spu(struct emulator *gameboy);
void update_spu_sound_amp(struct emulator *gameboy);
void start_spu_nr1(struct emulator *gameboy);
void start_spu_nr2(struct emulator *gameboy);
void start_spu_nr3(struct emulator *gameboy);
void start_spu_nr4(struct emulator *gameboy);
void reload_spu_duration(struct spu_duration *duration, unsigned max_duration, uint8_t t1);
void reload_spu_sweep(struct spu_sweep *sweep, uint8_t configuration);

#endif
