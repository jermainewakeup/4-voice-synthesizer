#include "daisy_pod.h"
#include "daisysp.h"
#include <stdio.h>
#include <string.h>

using namespace daisy;
using namespace daisysp;

#define NUM_VOICES 4

struct Voice
{
    Oscillator osc;
    Adsr       env;
    bool       active;
    int        note;
};

static DaisyPod hw;
static Voice voices[NUM_VOICES];
static Svf   filt;

float attack_time = 0.01f;
float decay_time = 0.1f;
float sustain_level = 0.7f;
float release_time = 0.2f;

void UpdateEnvelopeSettings()
{
    for(int i = 0; i < NUM_VOICES; i++)
    {
        voices[i].env.SetTime(ADSR_SEG_ATTACK, attack_time);
        voices[i].env.SetTime(ADSR_SEG_DECAY, decay_time);
        voices[i].env.SetSustainLevel(sustain_level);
        voices[i].env.SetTime(ADSR_SEG_RELEASE, release_time);
    }
}

void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t                                size)
{
    float sig, env_sig;
    for(size_t i = 0; i < size; i += 2)
    {
        sig = 0.0f;
        for(int v = 0; v < NUM_VOICES; v++)
        {
            if(voices[v].active)
            {
                env_sig = voices[v].env.Process(true);
                sig += voices[v].osc.Process() * env_sig;
                if(!voices[v].env.IsRunning()) // Turn off voice if envelope is finished
                {
                    voices[v].active = false;
                }
            }
        }
        filt.Process(sig);
        out[i] = out[i + 1] = filt.Low();
    }
}

int FindFreeVoice()
{
    for(int i = 0; i < NUM_VOICES; i++)
    {
        if(!voices[i].active)
            return i;
    }
    return -1;
}

void HandleMidiMessage(MidiEvent m)
{
    switch(m.type)
    {
        case NoteOn:
        {
            NoteOnEvent p = m.AsNoteOn();
            if(p.velocity != 0) // Note On with non-zero velocity
            {
                int voice = FindFreeVoice();
                if(voice >= 0)
                {
                    voices[voice].osc.SetFreq(mtof(p.note));
                    voices[voice].osc.SetAmp(1.0f); // Full amplitude, controlled by envelope
                    voices[voice].env.Retrigger(true); // Start the envelope
                    voices[voice].active = true;
                    voices[voice].note = p.note;
                    printf("NoteOn: Voice %d, Note %d\n", voice, p.note);
                }
            }
            else // Note On with zero velocity (equivalent to Note Off)
            {
                for(int v = 0; v < NUM_VOICES; v++)
                {
                    if(voices[v].note == p.note) // Find matching voice
                    {
                        voices[v].env.Retrigger(false); // Release the envelope
                        voices[v].active = false;
                        printf("NoteOff (zero velocity): Voice %d, Note %d\n", v, p.note);
                        break;
                    }
                }
            }
            break;
        }
        case NoteOff:
        {
            NoteOffEvent p = m.AsNoteOff();
            for(int v = 0; v < NUM_VOICES; v++)
            {
                if(voices[v].note == p.note) // Find matching voice
                {
                    voices[v].env.Retrigger(false); // Release the envelope
                    voices[v].active = false;
                    printf("NoteOff: Voice %d, Note %d\n", v, p.note);
                    break;
                }
            }
            break;
        }
        case ControlChange:
        {
            ControlChangeEvent p = m.AsControlChange();
            switch(p.control_number)
            {
                case 21:
                    // CC 1 for cutoff.
                    filt.SetFreq(mtof((float)p.value));
                    break;
                case 23:
                    // CC 2 for res.
                    filt.SetRes(((float)p.value / 127.0f));
                    break;
                case 24:
                    // CC 3 for attack time.
                    attack_time = (float)p.value / 127.0f;
                    UpdateEnvelopeSettings();
                    break;
                case 25:
                    // CC 4 for decay time.
                    decay_time = (float)p.value / 127.0f;
                    UpdateEnvelopeSettings();
                    break;
                case 26:
                    // CC 5 for sustain level.
                    sustain_level = (float)p.value / 127.0f;
                    UpdateEnvelopeSettings();
                    break;
                case 27:
                    // CC 6 for release time.
                    release_time = (float)p.value / 127.0f;
                    UpdateEnvelopeSettings();
                    break;
                default: break;
            }
            break;
        }
        default: break;
    }
}

int main(void)
{
    // Init
    float samplerate;
    hw.Init();
    hw.SetAudioBlockSize(4);
    hw.seed.usb_handle.Init(UsbHandle::FS_INTERNAL);
    System::Delay(250);

    // Synthesis
    samplerate = hw.AudioSampleRate();
    for(int i = 0; i < NUM_VOICES; i++)
    {
        voices[i].osc.Init(samplerate);
        voices[i].osc.SetWaveform(Oscillator::WAVE_SQUARE);
        voices[i].env.Init(samplerate);
        voices[i].active = false;
    }
    filt.Init(samplerate);

    UpdateEnvelopeSettings(); // Set initial ADSR settings

    // Start
    hw.StartAdc();
    hw.StartAudio(AudioCallback);
    hw.midi.StartReceive();

    printf("Initialization complete\n");

    for(;;)
    {
        hw.midi.Listen();
        // Handle MIDI Events
        while(hw.midi.HasEvents())
        {
            HandleMidiMessage(hw.midi.PopEvent());
        }
    }
}
