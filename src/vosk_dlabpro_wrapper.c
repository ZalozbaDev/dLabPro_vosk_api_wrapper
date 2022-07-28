
#include "vosk_api.h"

#include "recognizer_vosk_wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <portaudio.h>

struct VoskModel
{
	int       instanceId;
	pthread_t recognizerThreadId;
};

static int voskModelInstanceId = 1;

struct VoskRecognizer
{
	int instanceId;
	int modelInstanceId;
};

static int voskRecognizerInstanceId = 1;

static VoskRecognizer dummyRecognizer;

static int silence_ctr = 0;

static char* recognizer_argv[] = {"", "-cfg", "recognizer.cfg", "-out", "res"};  

static void* recognizerThread(void* arg)
{
	recognizer_main((sizeof(recognizer_argv) / sizeof(char*)), ((char**) &recognizer_argv));
}

VoskModel *vosk_model_new(const char *model_path)
{
	VoskModel* instance;
	printf("vosk_model_new, path=%s, instance=%d.\n", model_path, voskModelInstanceId);
	
	instance = (VoskModel*) malloc(sizeof(VoskModel));
	instance->instanceId = voskModelInstanceId;
	
	// start the thread for the recognizer here (assure one instance only)
	if (voskModelInstanceId == 1)
	{
		int retVal = pthread_create(&instance->recognizerThreadId,
			NULL,
			recognizerThread,
			NULL);
		
		if (retVal != 0)
		{
			printf("recognizer thread start error: %d.\n", retVal);	
		}
	}
	
	voskModelInstanceId++;
	return instance;
}

void vosk_model_free(VoskModel *model)
{
	printf("vosk_model_free, instance=%d\n", model->instanceId);

	// destroying the last model shall also end the recognizer thread
	if (voskModelInstanceId == 2)
	{
		recognizer_exit();
		
		int retVal = pthread_join(model->recognizerThreadId, NULL);

		if (retVal != 0)
		{
			printf("recognizer thread join error: %d.\n", retVal);	
		}
	}
	
	free(model);
	
	voskModelInstanceId--;
}

VoskRecognizer *vosk_recognizer_new(VoskModel *model, float sample_rate)
{
	VoskRecognizer* instance;
	printf("vosk_recognizer_new, sample_rate=%.2f, instance=%d, modelInstaceId=%d.\n", sample_rate, voskRecognizerInstanceId, model->instanceId);
	
	instance = (VoskRecognizer*) malloc(sizeof(VoskRecognizer));
	instance->instanceId = voskRecognizerInstanceId;
	instance->modelInstanceId = model->instanceId;
	
	voskRecognizerInstanceId++;
	
	return instance;
}

void vosk_recognizer_free(VoskRecognizer *recognizer)
{
	printf("vosk_recognizer_free, instance=%d\n", recognizer->instanceId);
	
	free(recognizer);
	
	voskRecognizerInstanceId--;
}

void vosk_recognizer_set_max_alternatives(VoskRecognizer *recognizer, int max_alternatives)
{
	printf("vosk_recognizer_set_max_alternatives, instance=%d, max_alternatives=%d.\n", recognizer->instanceId, max_alternatives);
}

void vosk_recognizer_set_words(VoskRecognizer *recognizer, int words)
{
	printf("vosk_recognizer_set_words, instance=%d, words=%d.\n", recognizer->instanceId, words);
}

int vosk_recognizer_accept_waveform(VoskRecognizer *recognizer, const char *data, int length)
{
	printf("vosk_recognizer_accept_waveform, instance=%d, length=%d.\n", recognizer->instanceId, length);
	if (silence_ctr < 5)
	{
		silence_ctr++;
		printf("vosk_recognizer_accept_waveform --> continue decoding\n");
		return 0;
	}
	silence_ctr = 0;
	printf("vosk_recognizer_accept_waveform --> get result\n");
	return 1;
}

const char *result_text="{ \"text\" : \"Result text\" }";

const char *vosk_recognizer_result(VoskRecognizer *recognizer)
{
	printf("vosk_recognizer_result, instance=%d\n", recognizer->instanceId);
	return result_text;
}

const char *partial_result_text="{ \"partial\" : \"Partial result text\" }";

const char *vosk_recognizer_partial_result(VoskRecognizer *recognizer)
{
	printf("vosk_recognizer_partial_result, instance=%d\n", recognizer->instanceId);
	return partial_result_text;
}

const char *final_result_text="{ \"text\" : \"Final result text\" }";

const char *vosk_recognizer_final_result(VoskRecognizer *recognizer)
{
	printf("vosk_recognizer_final_result, instance=%d\n", recognizer->instanceId);
	return final_result_text;
}

//////////////////////////////////////////////////////////////////
//
// fake portaudio
//
//////////////////////////////////////////////////////////////////

PaDeviceIndex Pa_GetDeviceCount( void )
{
	return 1;	
}

static PaDeviceInfo pdInfo
{
	.structVersion = 2,
	.name = "vosk",
	.hostApi = 1,
	.maxInputChannels = 1,
	.maxOutputChannels = 1,
	.defaultLowInputLatency = 1.0,
	.defaultLowOutputLatency = 1.0,
	.defaultHighInputLatency = 2.0,
	.defaultHighOutputLatency = 2.0,
	.defaultSampleRate = 8000.0
};

const PaDeviceInfo* Pa_GetDeviceInfo( PaDeviceIndex device )
{
	return &pdInfo;
}

PaError Pa_Initialize( void )
{
	return paNoError;
}

PaError Pa_Terminate( void )
{
	return paNoError;
}

PaError Pa_OpenStream( PaStream** stream,
                       const PaStreamParameters *inputParameters,
                       const PaStreamParameters *outputParameters,
                       double sampleRate,
                       unsigned long framesPerBuffer,
                       PaStreamFlags streamFlags,
                       PaStreamCallback *streamCallback,
                       void *userData )
{
	return paNoError;
}


PaError Pa_OpenDefaultStream( PaStream** stream,
                              int numInputChannels,
                              int numOutputChannels,
                              PaSampleFormat sampleFormat,
                              double sampleRate,
                              unsigned long framesPerBuffer,
                              PaStreamCallback *streamCallback,
                              void *userData )
{
	return paNoError;
}

PaError Pa_CloseStream( PaStream *stream )
{
	return paNoError;
}

PaError Pa_StartStream( PaStream *stream )
{
	return paNoError;
}

PaError Pa_StopStream( PaStream *stream )
{
	return paNoError;
}


