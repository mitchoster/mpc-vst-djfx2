#include "JustRepeat.h"


const double JustRepeat::DIVS[NUM_DIVS] = {
    1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64,
};


JustRepeat::JustRepeat(audioMasterCallback audioMaster) : AudioEffectX(audioMaster, 1, NUM_PARAMS) {
    barTime = sampleRate * 4;

    setNumInputs(2);
    setNumOutputs(2);

    setUniqueID('JRpt');

    resume();
}


void JustRepeat::resume() {
    AudioEffectX::resume();
}


void JustRepeat::setParameter(VstInt32 index, float value) {
    switch(index) {
        case PARAM_REPEAT:
            value = value < 0.5f ? 0.f : 1.f;
            if(pRepeat != value) {
                if(value == 1) {
                    barTime = 240.0 / getTempo();
                    recSamples = (int) floor(barTime * sampleRate);
                    if(recSamples > BUF_LEN)
                        recSamples = BUF_LEN;
                    setPlaySamples();
                    recPos = 0;
                    state = REC;
                }
                else {
                    state = STOP;
                }
            }
            pRepeat = value;
            break;
        case PARAM_LENGTH:
            if(pLength != value) {
                div = (int) roundf(value * (float) (NUM_DIVS - 1));
                setPlaySamples();
            }
            pLength = value;
            break;
        default:
            break;
    }
}


float JustRepeat::getParameter(VstInt32 index) {
    float v = 0;

    switch(index) {
        case PARAM_REPEAT:
            v = pRepeat;
            break;
        case PARAM_LENGTH:
            v = pLength;
            break;
        default:
            break;
    }

    return v;
}


void JustRepeat::getParameterName(VstInt32 index, char *label) {
    switch(index) {
        case PARAM_REPEAT:
            strcpy(label, "Repeat");
            break;
        case PARAM_LENGTH:
            strcpy(label, "Length");
            break;
        default:
            strcpy(label, "");
            break;
    }
}


void JustRepeat::getParameterDisplay(VstInt32 index, char *text) {
    switch(index) {
        case PARAM_REPEAT:
            strcpy(text, pRepeat < 0.5f ? "Off" : "On");
            break;
        case PARAM_LENGTH:
            strcpy(text, "1/");
            float2string(
                    (float) DIVS[div],
                    text + 2,
                    (double) (int) DIVS[div] == DIVS[div] ? (DIVS[div] < 10 ? 1 : 2) : 3
            );
            break;
        default:
            break;
    }
}


void JustRepeat::getParameterLabel(VstInt32 index, char *label) {
    strcpy(label, "");
}


bool JustRepeat::getEffectName(char *name) {
    strcpy(name, "JustRepeat");
    return true;
}


bool JustRepeat::getProductString(char *text) {
    strcpy(text, "JustRepeat");
    return true;
}


bool JustRepeat::getVendorString(char *text) {
    strcpy(text, "WrongPeople");
    return true;
}


void JustRepeat::processReplacing(float **inputs, float **outputs, VstInt32 sampleFrames) {
    for(int s = 0; s < sampleFrames; s++) {

        if(state == STOP) {
            processBypass(s, inputs, outputs);
            continue;
        }

        if(state == REC) {

            if(recPos < playSamples) {
                processRec(s, inputs, outputs);
                processBypass(s, inputs, outputs);
                recPos++;
            }
            else {
                playPos = 0;
                totalPlayPos = 0;
                state = PLAY;
            }

        }

        if(state == PLAY) {

            if(recPos < recSamples) {
                processRec(s, inputs, outputs);
                recPos++;
            }

            processPlay(s, inputs, outputs);

            playPos++;
            totalPlayPos++;

            if(playPos >= playSamples)
                playPos = 0;

            if(totalPlayPos >= recSamples)
                totalPlayPos = 0;

        }

    }
}


inline void JustRepeat::setPlaySamples() {
    int samples = (int) floor((barTime / DIVS[div]) * sampleRate);

    if(state == PLAY && playPos >= samples) {
        playPos = playPos % samples;
    }

    playSamples = samples;
}


inline void JustRepeat::processBypass(int s, float **inputs, float **outputs) {
    outputs[0][s] = inputs[0][s];
    outputs[1][s] = inputs[1][s];
}


inline void JustRepeat::processRec(int s, float **inputs, float **outputs) {
    buf[0][recPos] = inputs[0][s];
    buf[1][recPos] = inputs[1][s];
}


inline void JustRepeat::processPlay(int s, float **inputs, float **outputs) {
    outputs[0][s] = buf[0][playPos];
    outputs[1][s] = buf[1][playPos];
}


inline double JustRepeat::getTempo() {
    static VstTimeInfo *timeInfo;
    timeInfo = getTimeInfo(kVstTempoValid);
    return timeInfo != nullptr ? timeInfo->tempo : 60.0;
}