#pragma once

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "public.sdk/source/vst2.x/audioeffectx.h"


#define BUF_LEN (44100 * 32)
#define NUM_DIVS 13


class JustRepeat : public AudioEffectX {

    enum Param {
        PARAM_REPEAT,
        PARAM_LENGTH,
        NUM_PARAMS
    };

    enum State { STOP, REC, PLAY };

public:

    explicit JustRepeat(audioMasterCallback audioMaster);

    virtual void setParameter(VstInt32 index, float value);

    virtual float getParameter(VstInt32 index);

    virtual void getParameterLabel(VstInt32 index, char *label);

    virtual void getParameterDisplay(VstInt32 index, char *text);

    virtual void getParameterName(VstInt32 index, char *text);

    virtual void resume();

    virtual bool getEffectName(char *name);

    virtual bool getVendorString(char *text);

    virtual bool getProductString(char *text);

    virtual VstInt32 getVendorVersion() { return 1000; }

    virtual VstPlugCategory getPlugCategory() { return kPlugCategEffect; }

    virtual void processReplacing(float **inputs, float **outputs, VstInt32 sampleFrames);


protected:

    static const double DIVS[NUM_DIVS];

    float pRepeat = 0;
    float pLength = 0;

    int div = 0;

    int state = STOP;
    int recPos = 0;
    int playPos = 0;
    int recSamples = 0;
    int playSamples = 0;
    int totalPlayPos = 0;
    double barTime;

    float buf[2][BUF_LEN]{};

    inline void setPlaySamples();
    inline void processBypass(int s, float **inputs, float **outputs);
    inline void processRec(int s, float **inputs, float **outputs);
    inline void processPlay(int s, float **inputs, float **outputs);
    inline double getTempo();

};
