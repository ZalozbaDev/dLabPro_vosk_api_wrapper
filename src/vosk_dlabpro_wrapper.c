
#include "vosk_api.h"

#include "recognizer_vosk_wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

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
	float inputSampleRate;
};

static int voskRecognizerInstanceId = 1;

static VoskRecognizer dummyRecognizer;

static int silence_ctr = 0;

static char* recognizer_argv[] = {"", "-cfg", "recognizer.cfg", "-out", "vad"};  

static void* recognizerThread(void* arg)
{
	recognizer_main((sizeof(recognizer_argv) / sizeof(char*)), ((char**) &recognizer_argv));
}

static PaStreamCallback* audioStreamCallback;
static void* audioStreamUserData;

static int audioDecodingStatus = 0;

static float audioCallbackBuffer[PABUF_SIZE];
static int audioCallbackBufferPtr = 0;

static char resultBuffer[5000];

struct InstanceActivity
{
	int instanceId;
	int modelInstanceId;
	struct timeval activeTime;
};

static struct InstanceActivity activeInstance
{
	.instanceId         = -1,
	.modelInstanceId    = -1,
	.activeTime         = { .tv_sec  = 0, .tv_usec = 0 }
};

static int checkActiveInstance(VoskRecognizer *recognizer)
{
	struct timeval currTime;
	int acceptInstance = 0;

	if (gettimeofday(&currTime, NULL) != 0)
	{
		printf("Error in gettimeofday()!\n");	
	}
	
	// same instance --> just refresh times
	if ((recognizer->instanceId == activeInstance.instanceId) && (recognizer->modelInstanceId == activeInstance.modelInstanceId))
	{
		activeInstance.activeTime.tv_sec  = currTime.tv_sec;
		activeInstance.activeTime.tv_usec = currTime.tv_usec;
		acceptInstance = 1;
	}
	else
	{
		// last active instance wasn't active for at least 1..2 seconds, so make this instance the active one
		if (abs((int) activeInstance.activeTime.tv_sec - (int) 	currTime.tv_sec) > 2)
		{
			printf("Changing active instance form %d:%d to %d:%d.\n", activeInstance.instanceId, activeInstance.modelInstanceId, recognizer->instanceId, recognizer->modelInstanceId);
			activeInstance.instanceId      = recognizer->instanceId;
			activeInstance.modelInstanceId = recognizer->modelInstanceId;
			activeInstance.activeTime.tv_sec  = currTime.tv_sec;
			activeInstance.activeTime.tv_usec = currTime.tv_usec;
			acceptInstance = 1;
		}
	}
	
	return acceptInstance;
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
	instance->inputSampleRate = sample_rate;
	
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
	int retVal;
	
	printf("vosk_recognizer_accept_waveform, instance=%d, modelInstaceId=%d, length=%d, sampleRate=%.2f.\n", recognizer->instanceId, recognizer->modelInstanceId, length, recognizer->inputSampleRate);
	
	/*
	printf("%02X %02X %02X %02X %02X %02X %02X %02X\n",
		data[0], data[1], data[2], data[3],
		data[4], data[5], data[6], data[7]);
		*/
	
	// only serve the active instace
	if (checkActiveInstance(recognizer) == 1)
	{
		int idleCtr = recognizer_get_idle_counter();
		int busyCtr = recognizer_get_busy_counter();
		
		// TODO idle ctr != 0 should be sticky!
		if (idleCtr != 0)
		{
			int dataLength = 0;
			int callbackCalled = 0;
			
			printf("ACCEPT\n");
			
			// FIXME how to handle unaligned data?
			while (dataLength < length)
			{
				short value = (short) ((data[dataLength] & 0xFF) | ((data[dataLength + 1] & 0xFF) << 8));
				float fValue = (float) value;
				
				// float32 format for portaudio means values are between -1.0 and +1.0
				fValue /= 32768;
				
				/*
				if (audioCallbackBufferPtr < 10)
				{
					printf("(%02X %02X) %.2f ", data[dataLength], data[dataLength + 1], fValue);
				}
				if (audioCallbackBufferPtr == 10)
				{
					printf("\n");	
				}
				*/
				
				if ((recognizer->inputSampleRate != 8000.0) && (recognizer->inputSampleRate != 16000.0)
					&& (recognizer->inputSampleRate != 48000.0))
				{
					printf("Error! Unsupported sample rate=%.2f!\n", recognizer->inputSampleRate);	
				}
				
				audioCallbackBuffer[audioCallbackBufferPtr] = fValue;
				audioCallbackBufferPtr++;
				
				// double all samples for 8kHz input rate
				if (recognizer->inputSampleRate == 8000.0)
				{
					audioCallbackBuffer[audioCallbackBufferPtr] = fValue;
					audioCallbackBufferPtr++;
				}
				
				if (audioCallbackBufferPtr == PABUF_SIZE)
				{
					audioStreamCallback(audioCallbackBuffer, NULL, PABUF_SIZE, NULL, 0, audioStreamUserData);
					audioCallbackBufferPtr = 0;
					callbackCalled = 1;
				}
				
				dataLength += 2;
				
				// do an ugly downsampling for 48Khz input rate
				if (recognizer->inputSampleRate == 48000.0)
				{
					dataLength += 4;					
				}
			}
			
			if (callbackCalled != 0)
			{
				while (recognizer_get_busy_counter() == busyCtr)
				{
					printf("+");
					usleep(1000);
				}
				printf("\n");
				
				idleCtr = recognizer_get_idle_counter();
				
				while (recognizer_get_idle_counter() == idleCtr)
				{
					printf("-");
					usleep(1000);
				}
				printf("\n");
	
				if (recognizer_get_vad_status() == 1)
				{
					printf("O ");
					
					// we always need more data if VAD is active
					audioDecodingStatus = 1;
					retVal = 0;
				}
				else
				{
					// check if VAD was active before, if yes, we have a result
					if (audioDecodingStatus == 1) 
					{
						retVal = 1;	
					}
					else
					{
						retVal = 0;	
					}
					
					audioDecodingStatus = 0;
				}
			}
			else
			{
				// there wasn't even enough data to send to the recognizer
				retVal = 0;
			}
		}
		else
		{
			printf("IGNORE (not online)\n");
			
			// dunno what to return, try "partial"
			retVal = 0;
		}
	}
	else
	{
		printf("REJECT\n");
		
		// dunno what to return, try "partial"
		retVal = 0;
	}
	
	return retVal;
}

const char *partial_result_text_empty="{ \"partial\" : \"\" }";

const char *vosk_recognizer_partial_result(VoskRecognizer *recognizer)
{
	printf("vosk_recognizer_partial_result, instance=%d, modelInstaceId=%d\n", recognizer->instanceId, recognizer->modelInstanceId);
	
	// only serve the active instace
	// do not return partial result if VAD is off
	if ((checkActiveInstance(recognizer) == 1) && (recognizer_get_vad_status() == 1))
	{
		resultBuffer[0] = 0;
		
		printf("Partial result=%s.\n", recognizer_partial_result());
		
		strcat(resultBuffer, "{ \"partial\" : \"");
		strcat(resultBuffer, recognizer_partial_result());
		strcat(resultBuffer, "\" }");
		
		return resultBuffer;
	}
	else
	{
		return partial_result_text_empty;
	}
}

const char *result_text_empty="{ \"text\" : \"\" }";

const char *vosk_recognizer_result(VoskRecognizer *recognizer)
{
	printf("vosk_recognizer_result, instance=%d, modelInstaceId=%d\n", recognizer->instanceId, recognizer->modelInstanceId);

	// only serve the active instace
	if (checkActiveInstance(recognizer) == 1)
	{
		resultBuffer[0] = 0;
		
		printf("Result=%s.\n", recognizer_final_result());
		
		strcat(resultBuffer, "{ \"text\" : \"");
		strcat(resultBuffer, "-- ");
		strcat(resultBuffer, recognizer_final_result());
		strcat(resultBuffer, " --");
		strcat(resultBuffer, "\" }");
		
		recognizer_flush_results();
		
		return resultBuffer;
	}
	else
	{
		return result_text_empty;
	}
}

const char *vosk_recognizer_final_result(VoskRecognizer *recognizer)
{
	printf("vosk_recognizer_final_result, instance=%d, modelInstaceId=%d\n", recognizer->instanceId, recognizer->modelInstanceId);
	vosk_recognizer_result(recognizer);
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
	audioStreamCallback = streamCallback;
	audioStreamUserData = userData;
	
	// this is the only format that the dlabpro recognizer accepts
	assert(inputParameters->sampleFormat == paFloat32);
	assert(sampleRate == 16000.0);
	
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
	audioStreamCallback = streamCallback;
	audioStreamUserData = userData;

	// this is the only format that the dlabpro recognizer accepts
	assert(sampleFormat == paFloat32);
	assert(sampleRate == 16000.0);
	
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


