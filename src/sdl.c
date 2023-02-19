/*
 * Dylan Gilson
 * dylan.gilson@outlook.com
 * February 15, 2023
 */

#include <SDL.h>

#include "emulator.h"
#include "sdl.h"

#define UPSCALE_FACTOR 4

struct sdl_context {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *canvas;
    SDL_GameController *controller;
    SDL_AudioSpec audio_spec;
    SDL_AudioDeviceID audio_device;
    uint32_t pixels[GB_LCD_WIDTH * GB_LCD_HEIGHT * UPSCALE_FACTOR * UPSCALE_FACTOR];
    unsigned audio_buffer_index;
} sdl_context;

static void draw_line_dmg(struct emulator *gameboy, unsigned ly, union lcd_colour line[GB_LCD_WIDTH]) {
    struct sdl_context *context = gameboy->ui.data;

    static const uint32_t colour_map[4] = {
        [WHITE] = 0xFF75A32C,
        [LIGHT_GREY] = 0xFF387A21,
        [DARK_GREY] = 0xFF255116,
        [BLACK] = 0xFF12280B,
    };

    for (unsigned i = 0; i < GB_LCD_WIDTH; i++) {
        for (unsigned y = 0; y < UPSCALE_FACTOR; y++) {
            for (unsigned x = 0; x < UPSCALE_FACTOR; x++) {
                context->pixels[(ly + y) * GB_LCD_WIDTH * UPSCALE_FACTOR + i + x] = colour_map[line[i].dmg];
            }
        }
    }
}

static uint32_t sdl_5_to_8bits(uint32_t value) {
    return (value << 3) | (value >> 2);
}

static uint32_t gbc_to_xrgb8888(uint16_t colour) {
    uint32_t r = colour & 0x1F;
    uint32_t g = (colour >> 5) & 0x1F;
    uint32_t b = (colour >> 10) & 0x1F;
    uint32_t p;

    // extend from 5 to 8 bits
    r = sdl_5_to_8bits(r);
    g = sdl_5_to_8bits(g);
    b = sdl_5_to_8bits(b);

    p = 0xFF000000;
    p |= r << 16;
    p |= g << 8;
    p |= b;

    return p;
}

static void draw_line_gbc(struct emulator *gameboy, unsigned ly, union lcd_colour line[GB_LCD_WIDTH]) {
    struct sdl_context *context = gameboy->ui.data;

    for (unsigned i = 0; i < GB_LCD_WIDTH; i++) {
        uint16_t colour = line[i].gbc;

        for (unsigned y = 0; y < UPSCALE_FACTOR; y++) {
            for (unsigned x = 0; x < UPSCALE_FACTOR; x++) {
                context->pixels[(ly + y) * GB_LCD_WIDTH * UPSCALE_FACTOR + i + x] = gbc_to_xrgb8888(colour);
            }
        }
    }
}

static void handle_key(struct emulator *gameboy, SDL_Keycode key, bool pressed) {
    switch (key) {
        case SDLK_ESCAPE:
            if (pressed) {
                gameboy->quit = true;
            }
            break;
        case SDLK_RETURN:
            set_gamepad(gameboy, GB_INPUT_START, pressed);
            break;
        case SDLK_LSHIFT:
            set_gamepad(gameboy, GB_INPUT_SELECT, pressed);
            break;
        case SDLK_RSHIFT:
            set_gamepad(gameboy, GB_INPUT_SELECT, pressed);
            break;
        case SDLK_a:
            set_gamepad(gameboy, GB_INPUT_A, pressed);
            break;
        case SDLK_b:
            set_gamepad(gameboy, GB_INPUT_B, pressed);
            break;
        case SDLK_UP:
            set_gamepad(gameboy, GB_INPUT_UP, pressed);
            break;
        case SDLK_DOWN:
            set_gamepad(gameboy, GB_INPUT_DOWN, pressed);
            break;
        case SDLK_LEFT:
            set_gamepad(gameboy, GB_INPUT_LEFT, pressed);
            break;
        case SDLK_RIGHT:
            set_gamepad(gameboy, GB_INPUT_RIGHT, pressed);
            break;
    }
}

static void handle_button(struct emulator *gameboy, unsigned button, bool pressed) {
    // A and B are swapped between the GB and SDL (XBOX) conventions
    switch (button) {
        case SDL_CONTROLLER_BUTTON_START:
            set_gamepad(gameboy, GB_INPUT_START, pressed);
            break;
        case SDL_CONTROLLER_BUTTON_BACK:
            set_gamepad(gameboy, GB_INPUT_SELECT, pressed);
            break;
        case SDL_CONTROLLER_BUTTON_B:
            set_gamepad(gameboy, GB_INPUT_A, pressed);
            break;
        case SDL_CONTROLLER_BUTTON_A:
            set_gamepad(gameboy, GB_INPUT_B, pressed);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            set_gamepad(gameboy, GB_INPUT_UP, pressed);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            set_gamepad(gameboy, GB_INPUT_DOWN, pressed);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            set_gamepad(gameboy, GB_INPUT_LEFT, pressed);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            set_gamepad(gameboy, GB_INPUT_RIGHT, pressed);
            break;
    }
}

static void handle_new_controller(struct emulator *gameboy, unsigned index) {
    struct sdl_context *context = gameboy->ui.data;

    if (context->controller) {
        return; // controller already exists
    }

    if (!SDL_IsGameController(index)) {
        return;
    }

    context->controller = SDL_GameControllerOpen(index);
    if (context->controller != NULL) {
        printf("Using controller '%s'\n", SDL_GameControllerName(context->controller));
    }
}

static void find_controller(struct emulator *gameboy) {
    struct sdl_context *context = gameboy->ui.data;
    unsigned i;

    for (i = 0; context->controller == NULL && i < SDL_NumJoysticks(); i++) {
        handle_new_controller(gameboy, i);
    }
}

static void handle_controller_removed(struct emulator *gameboy, Sint32 which) {
    struct sdl_context *context = gameboy->ui.data;
    SDL_Joystick *joystick;

    if (!context->controller) {
        return;
    }

    joystick = SDL_GameControllerGetJoystick(context->controller);
    if (!joystick) {
        return;
    }

    if (SDL_JoystickInstanceID(joystick) == which) {
        /* the controller we were using has been removed */
        printf("Controller removed\n");
        SDL_GameControllerClose(context->controller);
        context->controller = NULL;

        /* Attempt to find a replacement */
        find_controller(gameboy);
    }
}

static void refresh_gamepad(struct emulator *gameboy) {
    SDL_Event event;

    while(SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                gameboy->quit = true;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                handle_key(gameboy, event.key.keysym.sym, (event.key.state == SDL_PRESSED));
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                handle_button(gameboy, event.cbutton.button, event.cbutton.state == SDL_PRESSED);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                handle_controller_removed(gameboy, event.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEADDED:
                handle_new_controller(gameboy, event.cdevice.which);
                break;
        }
    }
}

static void flip(struct emulator *gameboy) {
    struct sdl_context *context = gameboy->ui.data;

    SDL_UpdateTexture(context->canvas, NULL, context->pixels, GB_LCD_WIDTH * UPSCALE_FACTOR * sizeof(context->pixels[0])); // copy pixels to canvas
    SDL_RenderCopy(context->renderer, context->canvas, NULL, NULL); // render canvas
    SDL_RenderPresent(context->renderer);
}

static void destroy(struct emulator *gameboy) {
    struct sdl_context *context = gameboy->ui.data;

    if (context->controller) {
        SDL_GameControllerClose(context->controller);
    }

    SDL_DestroyTexture(context->canvas);
    SDL_DestroyRenderer(context->renderer);
    SDL_DestroyWindow(context->window);
    SDL_Quit();

    free(context);

    gameboy->ui.data = NULL;
}

static void audio_callback(void *userdata, Uint8 *stream, int length) {
    struct emulator *gameboy = userdata;
    struct sdl_context *context = gameboy->ui.data;
    struct spu_sample_buffer *buffer = &gameboy->spu.buffers[context->audio_buffer_index];

    if (sem_trywait(&buffer->ready) == 0) {
        memcpy(stream, buffer->samples, sizeof(buffer->samples));
        sem_post(&buffer->free); // refill SPU buffer

        context->audio_buffer_index = (context->audio_buffer_index + 1) % GB_SPU_SAMPLE_BUFFER_COUNT; // continue to next buffer
    } else {
        memset(stream, 0, sizeof(buffer->samples)); // buffer is not ready
    }
}

void init_sdl_ui(struct emulator *gameboy) {
    struct sdl_context *context;
    SDL_AudioSpec want;

    context = malloc(sizeof(*context));
    if (context == NULL) {
        perror("Malloc failed\n");
        exit(EXIT_FAILURE);
    }

    gameboy->ui.data = context;
    context->audio_buffer_index = 0;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    if (SDL_CreateWindowAndRenderer(GB_LCD_WIDTH * UPSCALE_FACTOR, GB_LCD_HEIGHT * UPSCALE_FACTOR, 0, &context->window, &context->renderer) < 0) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    SDL_SetWindowTitle(context->window, "GameBoy C");

    context->canvas = SDL_CreateTexture(context->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, GB_LCD_WIDTH, GB_LCD_HEIGHT);
    
    if (context->canvas == NULL) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    SDL_memset(&want, 0, sizeof(want));
    want.freq = GB_SPU_SAMPLE_RATE_HZ;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = GB_SPU_SAMPLE_BUFFER_LENGTH;
    want.callback = audio_callback;
    want.userdata = gameboy;
    context->audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &context->audio_spec, 0);

    if (context->audio_device == 0) {
        fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // start audio
    SDL_PauseAudioDevice(context->audio_device, 0);

    gameboy->ui.draw_line_dmg = draw_line_dmg;
    gameboy->ui.draw_line_gbc = draw_line_gbc;
    gameboy->ui.flip = flip;
    gameboy->ui.refresh_gamepad = refresh_gamepad;
    gameboy->ui.destroy = destroy;

    memset(context->pixels, 0, sizeof(context->pixels)); // clear canvas
    flip(gameboy);

    context->controller = NULL;
    
    find_controller(gameboy);
}
