/*
The HotBite Game Engine

Copyright(c) 2023 Vicente Sirvent Orts

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#pragma comment(lib, "dsound.lib")
#include <dsound.h>
#include <inttypes.h>

namespace HotBite {
    namespace Engine {
        namespace Core {

            class SoundDevice
            {
            private:

               
                LPDIRECTSOUNDFULLDUPLEX     DSFD;
                LPDIRECTSOUND8              DS8; //to write to the device
                LPDIRECTSOUNDCAPTURE8       DSC; //to read from the device
                LPDIRECTSOUNDCAPTUREBUFFER8 DSCB8;

                DSCBUFFERDESC               dscbd;
                DSBUFFERDESC                dsbdesc;

                //sound device 
                static SoundDevice* sound_device;

            public:

                //one per app
                static constexpr int32_t FREQ = 44100;//Hz
                static constexpr int32_t CHANNELS = 2; // Audio stereo
                static constexpr int32_t BPS = 16; // bits per sample
                static constexpr int64_t AUDIO_PERIOD_MS = 30;
                static constexpr int32_t BUFFER_SAMPLES = (AUDIO_PERIOD_MS * FREQ * CHANNELS) / 1000;
                static constexpr int32_t BUFFER_BYTES = BUFFER_SAMPLES * 2;
                static constexpr int32_t BUFFER_OFFSET = BUFFER_BYTES * 2;

                LPDIRECTSOUND8 GetDevice(void);
                LPDIRECTSOUNDCAPTUREBUFFER8 GetCaptureDevice(void);
                static SoundDevice* Get();

                SoundDevice(void);
                virtual ~SoundDevice(void);
            };

            class SoundDeviceGrabber
            {
            private:

                LPDIRECTSOUNDBUFFER8 buffer;
                DWORD offset;
                WAVEFORMATEX out_wave_type;

                HRESULT CreateSecondaryBuffer(WAVEFORMATEX* = NULL);

            public:

                SoundDeviceGrabber(void);
                virtual ~SoundDeviceGrabber(void);

                void Run(void);
                void Stop(void);
                int  write(BYTE* pBufferData, long BufferLen);

            };

        }
    }
}