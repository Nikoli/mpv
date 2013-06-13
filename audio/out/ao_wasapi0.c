/*
 * Windows Wasapi event mode interface
 *
 * Copyright (c) 2013 Jonathan Yong <10walls@gmail.com>
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define COBJMACROS 1
#define _WIN32_WINNT 0x600

#include <malloc.h>
#include <stdlib.h>
#include <process.h>
#include <initguid.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <avrt.h>

#include "config.h"
#include "core/subopt-helper.h"
#include "audio/format.h"
#include "core/mp_msg.h"
#include "ao.h"

#define ring_buffer_count 64

/* 20 millisecond buffer? */
#define BUFFER_TIME 20000000.0
#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }

typedef struct wasapi0_state {
  HANDLE threadLoop;

  /* Init phase */
  int init_ret;
  HANDLE init_done;
  HANDLE fatal_error; /* signal to indicate unrecoverable error */

  /* Events */
  HANDLE hPause;

  /* Play */
  HANDLE hPlay;
  int is_playing;

  /* Reset */
  HANDLE hReset;

  /* uninit */
  HANDLE hUninit;
  LONG immed;

  /* volume control */
  HANDLE hGetvol, hSetvol, hDoneVol;
  DWORD vol_hw_support, status;
  float audio_volume;

  /* Buffers */
  CRITICAL_SECTION buffer_lock;
  size_t buffer_block_size; /* Size of each block in bytes */
  LONG read_block_ptr, write_block_ptr; /*Which block are we in?*/
  LONG write_ahead_count; /* how many blocks writer is ahead of reader? should be less than ring_buffer_count*/
  uintptr_t write_offset; /*offset while writing partial blocks, used only in main thread */
  REFERENCE_TIME minRequestedDuration; /* minimum wasapi buffer block size, in 100-nanosecond units */
  REFERENCE_TIME defaultRequestedDuration; /* default wasapi default block size, in 100-nanosecond units */
  UINT32 bufferFrameCount; /* wasapi buffer block size, number of frames, frame size at format.nBlockAlign */
  void *ring_buffer[ring_buffer_count]; /* each bufferFrameCount sized, owned by main thread */

  /* WASAPI handles, owned by other thread */
  IMMDeviceEnumerator *pEnumerator;
  IMMDevice *pDevice;
  IAudioClient *pAudioClient;
  IAudioRenderClient *pRenderClient;
  IAudioEndpointVolume *pEndpointVolume;
  HANDLE hFeed; /* wasapi event */
  HANDLE hTask; /* AV thread */
  DWORD taskIndex; /* AV task ID */
  WAVEFORMATEX format;

  /* We still need to support XP, don't use these functions directly, blob owned by main thread */
  struct {
    HMODULE hAvrt;
    HANDLE (WINAPI *pAvSetMmThreadCharacteristicsW)(LPCWSTR,LPDWORD);
    WINBOOL (WINAPI *pAvRevertMmThreadCharacteristics)(HANDLE);
  } VistaBlob;
} wasapi0_state;

static int fill_VistaBlob(wasapi0_state *state){
  if(!state) return 1;
  HMODULE hkernel32 = GetModuleHandleW(L"kernel32.dll");
  if(!hkernel32) return 1;
  WINBOOL (WINAPI *pSetDllDirectory)(LPCWSTR lpPathName) = (WINBOOL (WINAPI *)(LPCWSTR))GetProcAddress(hkernel32,"SetDllDirectoryW");
  WINBOOL (WINAPI *pSetSearchPathMode)(DWORD Flags) = (WINBOOL (WINAPI *)(DWORD))GetProcAddress(hkernel32,"SetSearchPathMode");
  if(pSetSearchPathMode) pSetDllDirectory(L""); /* Attempt to use safe search paths */
  if(pSetSearchPathMode) pSetSearchPathMode(BASE_SEARCH_PATH_ENABLE_SAFE_SEARCHMODE);
  state->VistaBlob.hAvrt = LoadLibraryW(L"avrt.dll");
  if(!state->VistaBlob.hAvrt) goto Exit;
  state->VistaBlob.pAvSetMmThreadCharacteristicsW = (HANDLE (WINAPI *)(LPCWSTR, LPDWORD))GetProcAddress(state->VistaBlob.hAvrt,"AvSetMmThreadCharacteristicsW");
  state->VistaBlob.pAvRevertMmThreadCharacteristics = (WINBOOL (WINAPI *)(HANDLE))GetProcAddress(state->VistaBlob.hAvrt,"AvRevertMmThreadCharacteristics");
  return 0;
  Exit:
  if(state->VistaBlob.hAvrt) FreeLibrary(state->VistaBlob.hAvrt);
  if(pSetSearchPathMode) pSetDllDirectory(NULL);
  return 1;
}

static int enum_formats(struct ao * const ao){
  WAVEFORMATEX format;
  DWORD hr;
  int bytes_per_sample;
  if(!ao || !ao->priv ) return -1;
  struct wasapi0_state* state = (struct wasapi0_state *)ao->priv;

  switch(ao->format){
    case AF_FORMAT_S24_LE:
      bytes_per_sample = 3;
      break;
    case AF_FORMAT_S16_LE:
      bytes_per_sample = 2;
      break;
    case AF_FORMAT_U8:
      bytes_per_sample = 1;
      break;
    default:
      bytes_per_sample = 0;/* unsupported, fail it */
  }
  mp_msg(MSGT_AO, MSGL_V,"samplerate %d\n", ao->samplerate);
  format.wFormatTag = WAVE_FORMAT_PCM; /* Only PCM is supported */
  format.nChannels = ao->channels.num;
  format.nSamplesPerSec = ao->samplerate;
  format.nAvgBytesPerSec = format.nChannels * bytes_per_sample * format.nSamplesPerSec;
  format.nBlockAlign = format.nChannels * bytes_per_sample;
  format.wBitsPerSample = bytes_per_sample * 8;
  format.cbSize = 0;
  if((hr = IAudioClient_IsFormatSupported(state->pAudioClient,AUDCLNT_SHAREMODE_EXCLUSIVE, &format, NULL)) == S_OK){
    state->format = format;
    ao->bps = format.nAvgBytesPerSec;
    return 0;
  } else { /* Try default, 16bit @ 44.1kHz stereo */
    format.nChannels = 2;
    mp_chmap_from_channels(&ao->channels, 2);
    bytes_per_sample = 2;
    format.nSamplesPerSec = 44100;
    format.nAvgBytesPerSec = format.nChannels * bytes_per_sample * format.nSamplesPerSec;
    format.nBlockAlign = format.nChannels * bytes_per_sample;
    format.wBitsPerSample = 16;
    if((hr = IAudioClient_IsFormatSupported(state->pAudioClient,AUDCLNT_SHAREMODE_EXCLUSIVE, &format, NULL)) == S_OK){
      ao->samplerate = format.nSamplesPerSec;
      ao->bps = format.nAvgBytesPerSec;
      ao->format = AF_FORMAT_S16_LE;
      state-> format = format;
      return 0;
    }
  }
  return -1;
}

static int fix_format(struct wasapi0_state* state){
  HRESULT hr;
  double offset = 0.5;

  /* cargo cult code */
  EnterCriticalSection(&state->buffer_lock);
  hr = IAudioClient_GetDevicePeriod(state->pAudioClient,&state->defaultRequestedDuration, &state->minRequestedDuration);
  reinit:
  hr = IAudioClient_Initialize(state->pAudioClient,
                       AUDCLNT_SHAREMODE_EXCLUSIVE,
                       AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                       state->defaultRequestedDuration,
                       state->defaultRequestedDuration,
                       &state->format,
                       NULL);
  /* something about buffer sizes on Win7, fixme might loop forever */
  if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED){
    if (offset > 10.0) goto Exit; /* is 10 enough to break out of the loop?*/
    IAudioClient_GetBufferSize(state->pAudioClient,&state->bufferFrameCount);
	state->defaultRequestedDuration = (REFERENCE_TIME)((BUFFER_TIME / state->format.nSamplesPerSec * state->bufferFrameCount) + offset);
    offset += 0.5;
	IAudioClient_Release(state->pAudioClient);
	state->pAudioClient = NULL;
	hr = IMMDeviceActivator_Activate(state->pDevice,
                  &IID_IAudioClient, CLSCTX_ALL,
                  NULL, (void**)&state->pAudioClient);
    goto reinit;
  }
  EXIT_ON_ERROR(hr)
  hr = IAudioClient_GetService(state->pAudioClient,
                        &IID_IAudioRenderClient,
                        (void**)&state->pRenderClient);
  EXIT_ON_ERROR(hr)
  if(!state->hFeed) goto Exit;
  hr = IAudioClient_SetEventHandle(state->pAudioClient,state->hFeed);
  EXIT_ON_ERROR(hr);
  hr = IAudioClient_GetBufferSize(state->pAudioClient,&state->bufferFrameCount);
  EXIT_ON_ERROR(hr);
  state->buffer_block_size = state->format.nBlockAlign * state->bufferFrameCount;
  LeaveCriticalSection(&state->buffer_lock);
  state->hTask = state->VistaBlob.pAvSetMmThreadCharacteristicsW(L"Pro Audio", &state->taskIndex);
  mp_msg(MSGT_AO, MSGL_V, "ao-wasapi: fix_format OK at %lld!\n", state->buffer_block_size);
  return 0;
Exit:
  mp_msg(MSGT_AO, MSGL_V, "ao-wasapi: fix_format fails!\n");
  LeaveCriticalSection(&state->buffer_lock);
  SetEvent(state->fatal_error);
  return 1;
}

static int thread_init(struct ao *ao){
  struct wasapi0_state* state = (struct wasapi0_state *)ao->priv;
  HRESULT hr;
  CoInitialize(NULL);
  hr = CoCreateInstance(&CLSID_MMDeviceEnumerator,NULL,CLSCTX_ALL, &IID_IMMDeviceEnumerator,(void**)&state->pEnumerator);
  EXIT_ON_ERROR(hr)

  hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(state->pEnumerator,eRender, eConsole, &state->pDevice);
  EXIT_ON_ERROR(hr)

  hr = IMMDeviceActivator_Activate(state->pDevice,
                  &IID_IAudioClient, CLSCTX_ALL,
                  NULL, (void**)&state->pAudioClient);
  EXIT_ON_ERROR(hr)

  hr = IMMDeviceActivator_Activate(state->pDevice,
                  &IID_IAudioEndpointVolume, CLSCTX_ALL,
                  NULL, (void**)&state->pEndpointVolume);
  EXIT_ON_ERROR(hr)
  IAudioEndpointVolume_QueryHardwareSupport(state->pEndpointVolume,&state->vol_hw_support);

  state->init_ret = enum_formats(ao); /* Probe support formats */
  if(state->init_ret) goto Exit;
  fix_format(state); /* now that we're sure what format to use */
  mp_msg(MSGT_AO, MSGL_V, "ao-wasapi: thread_init OK!\n");
  SetEvent(state->init_done);
  return state->init_ret;
  Exit:
  mp_msg(MSGT_AO, MSGL_V, "ao-wasapi: thread_init fails!\n");
  state->init_ret = -1;
  SetEvent(state->init_done);
  return -1;
}

static void thread_pause(wasapi0_state *state){
  IAudioClient_Stop(state->pAudioClient);
}

static void thread_reset(wasapi0_state *state){
  IAudioClient_Stop(state->pAudioClient);
  IAudioClient_Reset(state->pAudioClient);
}

static void thread_feed(wasapi0_state *state){
  BYTE *pData;
  HRESULT hr = IAudioRenderClient_GetBuffer(state->pRenderClient,state->bufferFrameCount, &pData);
  EXIT_ON_ERROR(hr)
  EnterCriticalSection(&state->buffer_lock);
  if(state->write_ahead_count > 0){ /* OK to copy! */
    CopyMemory(pData,state->ring_buffer[state->read_block_ptr],state->buffer_block_size);
    state->read_block_ptr++;
    state->read_block_ptr = state->read_block_ptr % ring_buffer_count;
    state->write_ahead_count--;
    LeaveCriticalSection(&state->buffer_lock);
  } else {
    LeaveCriticalSection(&state->buffer_lock);
    /* buffer underrun?! abort */
    hr = IAudioRenderClient_ReleaseBuffer(state->pRenderClient,state->bufferFrameCount, AUDCLNT_BUFFERFLAGS_SILENT);
    EXIT_ON_ERROR(hr)
    goto Exit;
  }
  hr = IAudioRenderClient_ReleaseBuffer(state->pRenderClient,state->bufferFrameCount, 0);
  EXIT_ON_ERROR(hr)
  return;
Exit:
  mp_msg(MSGT_AO, MSGL_V, "ao-wasapi: thread_feed fails with %lx!\n", hr);
  return;
}

static void thread_play(wasapi0_state *state){
  thread_feed(state);
  IAudioClient_Start(state->pAudioClient);
  return;
}

static void thread_getVol(wasapi0_state *state){
  IAudioEndpointVolume_GetMasterVolumeLevelScalar(state->pEndpointVolume,&state->audio_volume);
  SetEvent(state->hDoneVol);
}

static void thread_setVol(wasapi0_state *state){
  IAudioEndpointVolume_SetMasterVolumeLevelScalar(state->pEndpointVolume,state->audio_volume,NULL);
  SetEvent(state->hDoneVol);
}

static void thread_uninit(wasapi0_state *state){
  if(!state->immed){
   /* feed until empty */
    while(1){
      EnterCriticalSection(&state->buffer_lock);
      LONG ahead = state->write_ahead_count;
      LeaveCriticalSection(&state->buffer_lock);
      if(WaitForSingleObject(state->hFeed,2000) == WAIT_OBJECT_0 && ahead){
          thread_feed(state);
      } else break;
    }
  }
  if(state->pAudioClient) IAudioClient_Stop(state->pAudioClient);
  if(state->pRenderClient) IAudioRenderClient_Release(state->pRenderClient);  
  if(state->pAudioClient) IAudioClient_Release(state->pAudioClient);
  if(state->pEnumerator) IMMDeviceEnumerator_Release(state->pEnumerator);
  if(state->pDevice) IMMDevice_Release(state->pDevice);
  if(state->hTask) state->VistaBlob.pAvRevertMmThreadCharacteristics(state->hTask);
  CoUninitialize();
  ExitThread(0);
}

static unsigned int __stdcall ThreadLoop(void *lpParameter){
  struct ao *ao = lpParameter;
  int feedwatch = 0;
  if(!ao || !ao->priv ) return -1;
  struct wasapi0_state* state = (struct wasapi0_state *)ao->priv;
  if(thread_init(ao)) {
    mp_msg(MSGT_AO, MSGL_ERR, "ao-wasapi: thread_init failed!\n");
    goto Exit;
  }

  DWORD waitstatus = WAIT_FAILED;
  HANDLE playcontrol[] = {state->hUninit,state->hPause,state->hReset,
    state->hGetvol, state->hSetvol, state->hPlay, state->hFeed,NULL};

  mp_msg(MSGT_AO, MSGL_V, "ao-wasapi: Entering dispatch loop!\n");
  while(1){ /* watch events, poll at least every 2 seconds */
    waitstatus = WaitForMultipleObjects(7,playcontrol,FALSE,2000);
    switch(waitstatus){
      case WAIT_OBJECT_0: /*shutdown*/
        feedwatch = 0;
        thread_uninit(state);
        goto Exit;
      case (WAIT_OBJECT_0+1): /* pause */
        feedwatch = 0;
        thread_pause(state);
        break;
      case (WAIT_OBJECT_0+2): /* reset */
        feedwatch = 0;
        thread_reset(state);
        break;
      case (WAIT_OBJECT_0+3): /* getVolume */
        thread_getVol(state);
        break;
      case (WAIT_OBJECT_0+4): /* setVolume */
        thread_setVol(state);
        break;
      case (WAIT_OBJECT_0+5): /* play */
        feedwatch = 0;
        thread_play(state);
        break;
      case (WAIT_OBJECT_0+6): /* feed */
        feedwatch = 1;
        thread_feed(state);
        break;
      case WAIT_TIMEOUT: /* Did our feed die? */
        if(feedwatch) return -1;
        break;
      default:
      case WAIT_FAILED: /* ??? */
        return -1;
    }
  }
  Exit:
  return 0;
}

static void closehandles(struct ao *ao){
  if(!ao || !ao->priv) return;
  struct wasapi0_state* state = (struct wasapi0_state *)ao->priv;
  if(state->init_done) CloseHandle(state->init_done);
  if(state->hPlay) CloseHandle(state->hPlay);
  if(state->hPause) CloseHandle(state->hPause);
  if(state->hReset) CloseHandle(state->hReset);
  if(state->hUninit) CloseHandle(state->hUninit);
  if(state->hFeed) CloseHandle(state->hFeed);
  if(state->hGetvol) CloseHandle(state->hGetvol);
  if(state->hSetvol) CloseHandle(state->hSetvol);
  if(state->hDoneVol) CloseHandle(state->hDoneVol);
}

static void uninit(struct ao *ao, bool immed){
  if(!ao || !ao->priv) return;
  struct wasapi0_state* state = (struct wasapi0_state *)ao->priv;
  state->immed = immed;
  SetEvent(state->hUninit);
  /* wait up to 10 seconds */
  if(WaitForSingleObject(state->threadLoop, 10000) == WAIT_TIMEOUT){
    SetEvent(state->fatal_error);
  }
  if(state->VistaBlob.hAvrt)
    FreeLibrary(state->VistaBlob.hAvrt);
  DeleteCriticalSection(&state->buffer_lock);
  ao->priv = NULL;
}

static int get_space(struct ao *ao){
  int ret = 0;
  if(!ao || !ao->priv) return -1;
  struct wasapi0_state* state = (struct wasapi0_state *)ao->priv;
  EnterCriticalSection(&state->buffer_lock);
  LONG ahead = state->write_ahead_count;
  size_t block_size = state->buffer_block_size;
  LeaveCriticalSection(&state->buffer_lock);
  ret = (ring_buffer_count - ahead) * block_size; /* rough */
  //mp_msg(MSGT_AO, MSGL_V, "ao-wasapi: get_space %lld!\n", (ret - (block_size - state->write_offset)));
  return (ret - (block_size - state->write_offset)); /* take offset into account */
}

static void reset_buffers(struct wasapi0_state* state){
  EnterCriticalSection(&state->buffer_lock);
  state->read_block_ptr = state->write_block_ptr = 0;
  state->write_ahead_count = 0;
  state->write_offset = 0;
  LeaveCriticalSection(&state->buffer_lock);
}

static void free_buffers(struct wasapi0_state* state){
  int iter;
  for(iter = 0; iter < ring_buffer_count; iter++){
    if(state->ring_buffer[iter]) _aligned_free(state->ring_buffer[iter]); /* msvcr* free can't handle null properly */
    state->ring_buffer[iter] = NULL;
  }
}

static int setup_buffers(struct wasapi0_state* state){
  int iter;
  reset_buffers(state);
  for(iter = 0; iter < ring_buffer_count; iter++){
    state->ring_buffer[iter] = _aligned_malloc(state->buffer_block_size,4);
    if(!state->ring_buffer[iter]){
      free_buffers(state);
      return 1; /* failed */
    }
  }
  return 0;
}

static int init(struct ao *ao, char *params){
  mp_msg(MSGT_AO, MSGL_V, "ao-wasapi0: init!\n");
  struct wasapi0_state *state = calloc(1, sizeof(struct wasapi0_state));
  if(!state) return -1;
  ao->priv = (void *)state;
  fill_VistaBlob(state);
  state->init_done = CreateEventW(NULL,FALSE,FALSE,NULL);
  state->hPlay = CreateEventW(NULL,FALSE,FALSE,NULL); /* kick start audio feed */
  state->hPause = CreateEventW(NULL,FALSE,FALSE,NULL);
  state->hReset = CreateEventW(NULL,FALSE,FALSE,NULL);
  state->hGetvol = CreateEventW(NULL,FALSE,FALSE,NULL);
  state->hSetvol = CreateEventW(NULL,FALSE,FALSE,NULL);
  state->hDoneVol = CreateEventW(NULL,FALSE,FALSE,NULL);
  state->hUninit = CreateEventW(NULL,FALSE,FALSE,NULL);
  state->fatal_error = CreateEventW(NULL,TRUE,FALSE,NULL);
  state->hFeed = CreateEvent(NULL,FALSE,FALSE,NULL); /* for wasapi event mode */
  InitializeCriticalSection(&state->buffer_lock);
  if(!state->init_done || !state->fatal_error || !state->hPlay || !state->hPause || !state->hFeed || !state->hReset || !state->hGetvol || !state->hSetvol || !state->hDoneVol){
    closehandles(ao);
    /* failed to init events */
    return -1;
  }
  state->init_ret = -1;
  state->threadLoop = (HANDLE)_beginthreadex(NULL,0,&ThreadLoop,ao,0,NULL);
  if(!state->threadLoop){
    /* failed to init thread */
    mp_msg(MSGT_AO, MSGL_ERR, "ao-wasapi0: fail to create thread!\n");
    return -1;
  }
  WaitForSingleObject(state->init_done,INFINITE); /* wait on init complete */
  mp_msg(MSGT_AO, MSGL_V, "ao-wasapi0: Init Done!\n");
  if(setup_buffers(state)){
    mp_msg(MSGT_AO, MSGL_ERR, "ao-wasapi0: buffer setup failed!\n");
  }
  return state->init_ret;
}

static int control(struct ao *ao, enum aocontrol cmd, void *arg){
  if(!ao || !ao->priv || !arg) return -1;
  struct wasapi0_state* state = (struct wasapi0_state *)ao->priv;
  if(!(state->vol_hw_support & ENDPOINT_HARDWARE_SUPPORT_VOLUME))
    return CONTROL_UNKNOWN; /* hw does not support volume controls in exclusive mode */

  ao_control_vol_t *vol = (ao_control_vol_t *)arg;
  ResetEvent(state->hDoneVol);

  switch (cmd) {
    case AOCONTROL_GET_VOLUME:
      SetEvent(state->hGetvol);
      if(WaitForSingleObject(state->hDoneVol,100) ==  WAIT_OBJECT_0){
        vol->left = vol->right = 100.0f * state->audio_volume;
        return CONTROL_OK;
      }
      return CONTROL_UNKNOWN;
    case AOCONTROL_SET_VOLUME:
      state->audio_volume = vol->left/100.f;
      SetEvent(state->hSetvol);
      if(WaitForSingleObject(state->hDoneVol,100) ==  WAIT_OBJECT_0){
        return CONTROL_OK;
      }
      return CONTROL_UNKNOWN;
    default:
      return CONTROL_UNKNOWN;
    }
}

static void audio_resume(struct ao *ao){
  if(!ao || !ao->priv) return;
  struct wasapi0_state* state = (struct wasapi0_state *)ao->priv;
  ResetEvent(state->hPause);
  ResetEvent(state->hReset);
  SetEvent(state->hPlay);
}

static void audio_pause(struct ao *ao){
  if(!ao || !ao->priv) return;
  struct wasapi0_state* state = (struct wasapi0_state *)ao->priv;
  ResetEvent(state->hPlay);
  SetEvent(state->hPause);
}

static void reset(struct ao *ao){
  if(!ao || !ao->priv) return;
  struct wasapi0_state* state = (struct wasapi0_state *)ao->priv;
  ResetEvent(state->hPlay);
  SetEvent(state->hReset);
  reset_buffers(state);
}

static int play(struct ao *ao, void *data, int len, int flags){
  int ret = 0;
  unsigned char *dat = (unsigned char *)data;
  //mp_msg(MSGT_AO, MSGL_V,"len %d flags %d\n", len, flags);
  if(!ao || !ao->priv) return ret;
  struct wasapi0_state* state = (struct wasapi0_state *)ao->priv;
  if(WaitForSingleObject(state->fatal_error,0) == WAIT_OBJECT_0) {
    /* something bad happened */
    return ret;
  }

  /* round to nearest block size? */
  EnterCriticalSection(&state->buffer_lock);
  if(len > state->buffer_block_size){ /* data len is larger than block size, do block by clock copy */
    while((len > state->buffer_block_size) && ((ring_buffer_count - 1 )> state->write_ahead_count) ){
      if(ret + state->buffer_block_size > len) break; /* no more free buffers! */
      CopyMemory(state->ring_buffer[state->write_block_ptr], &dat[ret], state->buffer_block_size);
      state->write_block_ptr ++;
      state->write_block_ptr = state->write_block_ptr % ring_buffer_count;
      state->write_ahead_count++;
      ret += state->buffer_block_size;
    }
  } else if (flags & AOPLAY_FINAL_CHUNK) { /* copy in frams outside of block size only if last block */
    /* zero out and fill with whatever that is left */
    SecureZeroMemory(state->ring_buffer[state->write_block_ptr],state->buffer_block_size);
    CopyMemory(state->ring_buffer[state->write_block_ptr], &dat[ret], len);
    state->write_block_ptr ++;
    state->write_block_ptr = state->write_block_ptr % ring_buffer_count;
    state->write_ahead_count++;
    ret += state->buffer_block_size;
  } /* otherwise leave buffers outside of block alignment and let player figure it out */
  LeaveCriticalSection(&state->buffer_lock);

  if(!state->is_playing) {
    /* start playing */
    state->is_playing = 1;
    SetEvent(state->hPlay);
  }
  return ret;
}

static float get_delay(struct ao *ao){
  if(!ao || !ao->priv) return -1.0f;
  struct wasapi0_state* state = (struct wasapi0_state *)ao->priv;
  return (float)(ring_buffer_count * state->buffer_block_size - get_space(ao)) / (float)state->format.nAvgBytesPerSec;
}

const struct ao_driver audio_out_wasapi0 = {
    .info = &(const struct ao_info) {
        "Windows WASAPI audio output (event mode)",
        "wasapi0",
        "Jonathan Yong <10walls@gmail.com>",
        "0.1 Beta"
    },
    .init      = init,
    .uninit    = uninit,
    .control   = control,
    .get_space = get_space,
    .play      = play,
    .get_delay = get_delay,
    .pause     = audio_pause,
    .resume    = audio_resume,
    .reset     = reset,
};
