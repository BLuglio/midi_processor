#include "portaudio.h"
#include "portmidi.h"
#include "porttime.h"
#include "pa_ringbuffer.h"
#include "pa_util.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define SAMPLE_RATE 44100
#define NUM_SECONDS 3
#define NUM_SAMPLES (NUM_SECONDS * SAMPLE_RATE)
#define PI 3.14159265f
#define TWO_PI (3.14159265f * 2)
#define FREQUENCY 440

#define TABLE_LEN 512
#define SINE 0
#define SAW 1
#define SQUARE 2
#define TRIANGLE 3

#define FRAME_BLOCK_LEN 256

#define NOTE_ON 0x90  //hex 144
#define NOTE_OFF 0x80 // hex 128

#define MSGSZ 128

float squareTable[TABLE_LEN];
float sineTable[TABLE_LEN];
float sawTable[TABLE_LEN];
float triangleTable[TABLE_LEN];

/*
********* PORT MIDI PARAMETERS *********
*/
PmDeviceInfo *infoMidi;
PmError retval;
PmEvent msg[32];
PortMidiStream *mstream;
/*
********************************************* 
*/

/*
********* PORT AUDIO PARAMETERS *********
*/
PaError paErr;
PaStream *audioStream;
PaDeviceIndex idx;
PaDeviceInfo *infoAudio;
PaStreamParameters outputParameters;

/* Ring buffer (FIFO) for "communicating" towards audio callback */
PaUtilRingBuffer midiMsgBuf;

typedef struct
{
    int status;
    int byte1;
    int byte2;
    float time;
    int noteOn;

    // velocity? altri parametri?

} MidiMessage;

typedef struct _mydata
{
    float amp;
    float freq;
    int note;
} mydata;

/* Context for callback routine. */
typedef struct
{
    MidiMessage *messages[16]; /* Maximum 16 messages */
    unsigned noOfActiveMessages;

    /* Ring buffer (FIFO) for "communicating" towards audio callback */
    PaUtilRingBuffer rBufToRT;
    void *rBufToRTData;

    /* Ring buffer (FIFO) for "communicating" from audio callback */
    // PaUtilRingBuffer rBufFromRT;
    // void *rBufFromRTData;
} paTestData;

/*
********************************************* 
*/

int audio_callback(const void *inputBuffer,
                   void *outputBuffer,
                   unsigned long framesPerBuffer,
                   const PaStreamCallbackTimeInfo *timeInfo,
                   PaStreamCallbackFlags statusFlags,
                   void *userData)
{
    paTestData *data = (paTestData *)userData;
    MidiMessage *ptr = 0;
    PaUtil_ReadRingBuffer(&data->rBufToRT, &ptr, 1);

    printf("Received:\n\tByte 1: %d \n\tByte2: %d\n\tNote on: %d\n", ptr->byte1, ptr->byte2, ptr->noteOn);

    float *out = (float *)outputBuffer;
    for (int i = 0; i < framesPerBuffer; i++)
    {
        *out++ = 0.0;
    }
    return 0;
}

void fillSine()
{
    int j;
    for (j = 0; j < TABLE_LEN; j++)
    {
        sineTable[j] = (float)sin(2 * PI * j / TABLE_LEN);
    }
}

void fillTriangle()
{
    int j;
    for (j = 0; j < TABLE_LEN / 2; j++)
    {
        triangleTable[j] = 2 * (float)j / (float)(TABLE_LEN / 2) - 1;
    }
    for (j = TABLE_LEN / 2; j < TABLE_LEN; j++)
    {
        triangleTable[j] = 1 - (2 * (float)(j - TABLE_LEN / 2) / (float)(TABLE_LEN / 2));
    }
}

void fillSaw()
{
    int j;
    for (j = 0; j < TABLE_LEN; j++)
    {
        sawTable[j] = 1 - (2 * (float)j / (float)TABLE_LEN);
    }
}

void fillSquare()
{
    int j;
    for (j = 0; j < TABLE_LEN / 2; j++)
    {
        squareTable[j] = 1;
    }
    for (j = TABLE_LEN / 2; j < TABLE_LEN; j++)
    {
        squareTable[j] = -1;
    }
}

float noteToFreq(int note)
{
    int a = 440; //A - 440Hz
    printf("Note played: %d", note);
    return (a / 32) * pow(2, ((note - 9) / 12));
}

/*
truncating table-lookup oscillator
amp: amplitude, [-1, 1]
len: table length
*/

float oscillator(float *output, float amp, float freq, float *table, float *index, int len, int vecsize, float sr)
{
    float incr = freq * len / sr;
    for (int i = 0; i < vecsize; i++)
    {
        output[i] = amp * table[(int)(*index)];
        *index += incr;
        while (index >= len)
            index -= len;
        while (index < 0)
            index += len;
    }
    return *output;
}

void *read_midi_input()
{
    float frequency, duration;
    int counter, i;

    // msgid = msgget(key, msgflg);
    // message.mesg_type = 1;

    Pm_Initialize();

    int id = Pm_GetDefaultInputDeviceID();
    infoMidi = Pm_GetDeviceInfo(id);
    if (infoMidi == NULL)
    {
        printf("[MIDI] Could not open default input device (%d) \n", id);
        return -1;
    }
    printf("[MIDI] Opening input device: %s %s\n", infoMidi->interf, infoMidi->name);
    retval = Pm_OpenInput(&mstream, id, NULL, 512L, NULL, NULL);
    if (retval != pmNoError)
    {
        printf("error: %s \n", Pm_GetErrorText(retval));
        return -1;
    }
    else
    {
        int isRunning = 1;
        while (isRunning)
        {
            if (Pm_Poll(mstream))
            {

                counter = Pm_Read(mstream, msg, 32);
                for (i = 0; i < counter; i++)

                {
                    frequency = noteToFreq(Pm_MessageData1(msg[i].message));
                    // if ((Pm_MessageStatus(msg[i].message) & 0xF0) == NOTE_ON)
                    // {
                    //     printf("[MIDI] NOTE ON - %f Hz\n", frequency);
                    // }
                    // else if ((Pm_MessageStatus(msg[i].message) & 0xF0) == NOTE_OFF)
                    // {
                    //     printf("[MIDI] NOTE OFF\n");
                    // }

                    //PaUtil_WriteRingBuffer(&data->rBufFromRT, &data->waves[i], 1);

                    printf("[MIDI] status: %d, byte1: %d, byte2: %d, time: %.3f\n",
                           Pm_MessageStatus(msg[i].message),
                           Pm_MessageData1(msg[i].message),
                           Pm_MessageData2(msg[i].message),
                           msg[i].timestamp / 1000.);
                }
            }
        }
    }
}

int main()
{
    int waveform;
    float frequency, duration;
    int counter, i;
    int midiErr;

    //init wave tables

    fillSaw();
    fillSine();
    fillSquare();
    fillTriangle();

    paTestData data = {0};

    /* Initialize communication buffers (queues) */
    data.rBufToRTData = PaUtil_AllocateMemory(sizeof(MidiMessage *) * 256);
    if (data.rBufToRTData == NULL)
    {
        return 1;
    }
    PaUtil_InitializeRingBuffer(&data.rBufToRT, sizeof(MidiMessage *), 256, data.rBufToRTData);

    // data.rBufFromRTData = PaUtil_AllocateMemory(sizeof(MidiMessage *) * 256);
    // if (data.rBufFromRTData == NULL)
    // {
    //     return 1;
    // }
    // PaUtil_InitializeRingBuffer(&data.rBufFromRT, sizeof(MidiMessage *), 256, data.rBufFromRTData);

    paErr = Pa_Initialize();
    if (paErr == paNoError)
    {
        idx = Pa_GetDefaultOutputDevice();
        infoAudio = Pa_GetDeviceInfo(idx);
        printf("[AUDIO] Opening output device: %s\n", infoAudio->name);
        // mydata *data = (mydata *)malloc(sizeof(mydata));
        outputParameters.device = Pa_GetDefaultOutputDevice(); /* default output device */
        if (outputParameters.device == paNoDevice)
        {
            printf(stderr, "[AUDIO] Error: No default output device.\n");
        }
        outputParameters.channelCount = 2;         /* stereo output */
        outputParameters.sampleFormat = paFloat32; /* 32 bit floating point output */
        outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
        outputParameters.hostApiSpecificStreamInfo = NULL;

        paErr = Pa_OpenStream(&audioStream, NULL, &outputParameters, SAMPLE_RATE, FRAME_BLOCK_LEN, paDitherOff, audio_callback, &data);
        if (paErr != paNoError)
        {
            printf(stderr, "[AUDIO] Error: open stream.\n");
        }
        printf("[AUDIO] Output device opened without errors\n");
        paErr = Pa_StartStream(audioStream);

        pthread_t midiThread;
        pthread_create(&midiThread, NULL, read_midi_input, NULL);
    }
    MidiMessage *m = malloc(sizeof(MidiMessage *) * 256);
    int isRunning = 1;
    while (isRunning)
    {
        if (m != NULL)
        {

            //TODO: spostare lettura messaggi e scrittura nel buffer nel thread midi???

            m->byte1 = 127;
            m->byte2 = 53;
            m->noteOn = 1;
            m->status = 1;
            m->time = 1.008f;

            /* Post wave to audio callback */
            PaUtil_WriteRingBuffer(&data.rBufToRT, &m, 1);
            ++data.noOfActiveMessages;

            // printf("Starting wave at level = %.2f, attack = %.2lf, pos = %.2lf\n", level, attackTime, pos);
        }
    }

    // pthread_exit(NULL);
    // exit(0);

    // TODO: handle thread error and exit conditions
error:
    Pa_Terminate();
    Pa_StopStream(audioStream);  // stop the callback
    Pa_CloseStream(audioStream); // destroy the audio stream object
    if (data.rBufToRTData)
    {
        PaUtil_FreeMemory(data.rBufToRTData);
    }
    Pm_Close(mstream);
    Pm_Terminate();
    fprintf(stderr, "An error occured while using the portaudio stream\n");
    fprintf(stderr, "Error number: %d\n", paErr);
    fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(paErr));
    return 0;
}