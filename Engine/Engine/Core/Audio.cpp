#include "Audio.h"
#include <stdio.h>

using namespace HotBite::Engine::Core;

#define BUFF_PLAY out_wave_type.nAvgBytesPerSec

SoundDevice* SoundDevice::sound_device = nullptr;

SoundDevice::SoundDevice() {
    HRESULT hr;
    HWND hWnd = GetDesktopWindow();

    // wFormatTag, nChannels, nSamplesPerSec, mAvgBytesPerSec,
    // nBlockAlign, wBitsPerSample, cbSize
    WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 2, FREQ, FREQ * 2 * 2 , 4, 16, 0 };

    dscbd.dwSize = sizeof(DSCBUFFERDESC);
    dscbd.dwFlags = 0;
    dscbd.dwBufferBytes = FREQ;
    dscbd.dwReserved = 0;
    dscbd.lpwfxFormat = &wfx;
    dscbd.dwFXCount = 0;
    dscbd.lpDSCFXDesc = nullptr;

    // Set up DSBUFFERDESC structure. 
    memset(&dsbdesc, 0, sizeof(DSBUFFERDESC));
    dsbdesc.dwSize = sizeof(DSBUFFERDESC);
    dsbdesc.dwFlags = 0;
    dsbdesc.dwBufferBytes = FREQ;
    dsbdesc.lpwfxFormat = &wfx;

    LPDIRECTSOUNDBUFFER8 DSBuffer8;

    hr = DirectSoundFullDuplexCreate8(nullptr,//pcGuidCaptureDevice, 
        nullptr,//pcGuidRenderDevice,
        &dscbd,
        &dsbdesc,
        hWnd,
        DSSCL_PRIORITY,
        &DSFD,
        &DSCB8,
        &DSBuffer8,
        nullptr);

    if (FAILED(hr))
    {
        char error[256];
        sprintf(error, "FATAL ERROR!!!\n"
            "Can't access sound device. Check"
            " if other application is using "
            "your sound card\n");

        MessageBox(nullptr, error, MB_OK, 0);
        ExitProcess(0);
    }
    DSBuffer8->Release(); //don't need this one
    hr = DSFD->QueryInterface(IID_IDirectSound8, (void**)&DS8);
    DSCB8->Start(DSCBSTART_LOOPING);
    DSFD->Release();
}

SoundDevice::~SoundDevice() {
    DS8->Release();
}

SoundDevice* SoundDevice::Get() {
    if (sound_device == nullptr) {
        sound_device = new SoundDevice();
    }
    return sound_device;
}

LPDIRECTSOUND8 SoundDevice::GetDevice() {
    return DS8;
}

LPDIRECTSOUNDCAPTUREBUFFER8 SoundDevice::GetCaptureDevice() {
    return DSCB8;
}

SoundDeviceGrabber::SoundDeviceGrabber() {
    offset = 0;
    buffer = nullptr;
    // wFormatTag, nChannels, nSamplesPerSec, mAvgBytesPerSec,
    // nBlockAlign, wBitsPerSample, cbSize
    WAVEFORMATEX wfx = { WAVE_FORMAT_PCM, 2, SoundDevice::FREQ, SoundDevice::FREQ * 2 * 2 , 4, 16, 0 };
    CreateSecondaryBuffer(&wfx);
}

SoundDeviceGrabber::~SoundDeviceGrabber() {
    if (buffer) {
        Stop();
        buffer->Release();
    }
}


HRESULT SoundDeviceGrabber::CreateSecondaryBuffer(WAVEFORMATEX* waveFormat) {

    LPDIRECTSOUNDBUFFER  pBuffer;
    DSBUFFERDESC dsbdesc;

    if (buffer) {
        Stop();
        buffer->Release();
    }

    out_wave_type = *waveFormat;

    // Set up DSBUFFERDESC structure. 
    memset(&dsbdesc, 0, sizeof(DSBUFFERDESC));
    dsbdesc.dwSize = sizeof(DSBUFFERDESC);
    dsbdesc.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_LOCDEFER;
    dsbdesc.dwBufferBytes = BUFF_PLAY;
    dsbdesc.lpwfxFormat = &out_wave_type;

    // Create buffer. 
    int hr = SoundDevice::Get()->GetDevice()->CreateSoundBuffer(&dsbdesc, &pBuffer, nullptr);
    hr = pBuffer->QueryInterface(IID_IDirectSoundBuffer8, (void**)&buffer);
    offset = BUFF_PLAY / 2;
    buffer->Restore();
    pBuffer->Release();
    return hr;
}

int
SoundDeviceGrabber::write(BYTE* pBufferData, long BufferLen)
{
    void* lpvPtr1 = nullptr;
    DWORD   dwBytes1 = 0;
    void* lpvPtr2 = nullptr;
    DWORD   dwBytes2 = 0;
    HRESULT hr;

    unsigned long bufferStatus = 0;

    DWORD readPos, writePos;

    buffer->GetCurrentPosition(&readPos, &writePos);
    if (offset <= readPos && offset + BufferLen >= readPos) {
        buffer->SetCurrentPosition(0);
        offset = BUFF_PLAY / 6;
    }

    buffer->GetStatus(&bufferStatus);
    if (!(DSBSTATUS_PLAYING & bufferStatus)) {
        Run();
    }

    hr = buffer->Lock(offset, BufferLen, &lpvPtr1, &dwBytes1, &lpvPtr2, &dwBytes2, 0);

    if (DSERR_BUFFERLOST == hr) {
        buffer->Restore();
        hr = buffer->Lock(0, BufferLen, &lpvPtr1,
            &dwBytes1, &lpvPtr2, &dwBytes2, DSBLOCK_FROMWRITECURSOR);
    }

    if (SUCCEEDED(hr)) {
        // Write to pointers. 
        CopyMemory(lpvPtr1, pBufferData, dwBytes1);
        if (nullptr != lpvPtr2)
        {
            CopyMemory(lpvPtr2, pBufferData + dwBytes1, dwBytes2);
        }
        // Release the data back to DirectSound. 
        hr = buffer->Unlock(lpvPtr1, dwBytes1, lpvPtr2, dwBytes2);
        offset = (offset + BufferLen) % BUFF_PLAY;
    }
    return hr;
}

void SoundDeviceGrabber::Run() {
    unsigned long status;
    buffer->GetStatus(&status);

    if (!(status & DSBSTATUS_PLAYING)) {
        int hr = buffer->Play(0, 0, DSBPLAY_LOOPING);
    }
    return;
}

void SoundDeviceGrabber::Stop() {
    void* lpvPtr1 = nullptr;
    DWORD   dwBytes1 = 0;
    void* lpvPtr2 = nullptr;
    DWORD   dwBytes2 = 0;
    buffer->Lock(0, 0, &lpvPtr1, &dwBytes1, &lpvPtr2, &dwBytes2, DSBLOCK_ENTIREBUFFER);

    // Write to pointers. 
    memset(lpvPtr1, 0xff, dwBytes1);

    if (nullptr != lpvPtr2) {
        memset(lpvPtr2, 0xff, dwBytes2);
    }

    // Release the data back to DirectSound. 
    buffer->Unlock(lpvPtr1, dwBytes1, lpvPtr2, dwBytes2);
    int hr = buffer->Stop();
    hr = buffer->SetCurrentPosition(0);
    offset = BUFF_PLAY / 6;
    return;
}



