/*
 * host_sdl.c - Mac/desktop implementation of the BreezyBox sound symbols
 * (snd_stream_*, bt_keyboard_*, vTaskDelay) over SDL2, shared by the music
 * apps' host builds (moddy, beeper). Adapted from breezybox/apps/modplay:
 * the termios raw-mode handling is removed because breezy_tui owns the
 * terminal (tui_init/tui_shutdown); this file is audio-only. Console-only
 * apps (beeper's synth demo) add their own stdin raw mode (host_stdin.c).
 *
 * Host build only -- the device gets these symbols from the firmware's
 * elf_loader table (see the per-app buildelf.sh, which does not compile
 * this file).
 */

#include <stdio.h>
#include <stdint.h>

#include <SDL.h>

#define RING_FRAMES 4096   /* match the firmware's stream ring */

static SDL_AudioDeviceID g_dev = 0;
static int g_channels = 2;
static int g_volume = 100; /* percent; the firmware scales in hardware */

int snd_init(void)
{
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

int snd_stream_open(int rate, int channels)
{
    if (g_dev || (channels != 1 && channels != 2)) return -1;
    SDL_AudioSpec want = {0}, have;
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = (Uint8)channels;
    want.samples = 1024;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) {
        fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return -1;
    }
    g_channels = channels;
    SDL_PauseAudioDevice(g_dev, 0);
    return 0;
}

int snd_stream_space(void)
{
    if (!g_dev) return 0;
    int queued = (int)(SDL_GetQueuedAudioSize(g_dev) / (sizeof(int16_t) * g_channels));
    return queued >= RING_FRAMES ? 0 : RING_FRAMES - queued;
}

int snd_stream_write(const int16_t *frames, int nframes)
{
    if (!g_dev || !frames || nframes < 0) return -1;
    int space = snd_stream_space();
    if (nframes > space) nframes = space;
    if (g_volume >= 100) {
        SDL_QueueAudio(g_dev, frames,
                       (Uint32)(nframes * sizeof(int16_t) * g_channels));
        return nframes;
    }
    /* software gain stands in for the device's hardware volume */
    static int16_t scaled[RING_FRAMES * 2];
    int nsamp = nframes * g_channels;
    for (int i = 0; i < nsamp; i++)
        scaled[i] = (int16_t)((int32_t)frames[i] * g_volume / 100);
    SDL_QueueAudio(g_dev, scaled, (Uint32)(nsamp * sizeof(int16_t)));
    return nframes;
}

void snd_set_volume(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    g_volume = pct;
}

void snd_stream_close(void)
{
    if (!g_dev) return;
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
}

/* No SDL window, so no SDL key events: report "no keyboard" and let the
 * app use stdin via breezy_tui instead. */
int bt_keyboard_is_pressed(uint8_t keycode) { (void)keycode; return 0; }
int bt_keyboard_connected(void)             { return 0; }

void vTaskDelay(uint32_t ticks)
{
    SDL_Delay(ticks * 10);   /* device tick is 10 ms */
}
