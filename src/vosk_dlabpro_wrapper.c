
#include "vosk_api.h"

#include "recognizer_vosk_wrapper.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

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

static PaStreamCallback* audioStreamCallback;
static void* audioStreamUserData;

static int audioDecodingStatus = 0;

static float audioCallbackBuffer[PABUF_SIZE];
static int audioCallbackBufferPtr = 0;

static char resultBuffer[5000];

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
	int retVal;
	
	printf("vosk_recognizer_accept_waveform, instance=%d, modelInstaceId=%d, length=%d.\n", recognizer->instanceId, recognizer->modelInstanceId, length);
	
	/*
	printf("%02X %02X %02X %02X %02X %02X %02X %02X\n",
		data[0], data[1], data[2], data[3],
		data[4], data[5], data[6], data[7]);
		*/
	
	// only serve the first instace
	if ((recognizer->instanceId == 1) && (recognizer->modelInstanceId == 1))
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
				
				/*
				if (audioCallbackBufferPtr < 10)
				{
					printf("(%02X %02X) %.2f ", data[dataLength], data[dataLength + 1], (float) value);
				}
				if (audioCallbackBufferPtr == 10)
				{
					printf("\n");	
				}
				*/
				
				audioCallbackBuffer[audioCallbackBufferPtr] = (float) value;
				audioCallbackBufferPtr++;
				audioCallbackBuffer[audioCallbackBufferPtr] = (float) value;
				audioCallbackBufferPtr++;
				
				if (audioCallbackBufferPtr == PABUF_SIZE)
				{
					audioStreamCallback(audioCallbackBuffer, NULL, PABUF_SIZE, NULL, 0, audioStreamUserData);
					audioCallbackBufferPtr = 0;
					callbackCalled = 1;
				}
				
				dataLength += 2;
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
	
	// only serve the first instace
	if ((recognizer->instanceId == 1) && (recognizer->modelInstanceId == 1))
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

	// only serve the first instace
	if ((recognizer->instanceId == 1) && (recognizer->modelInstanceId == 1))
	{
		resultBuffer[0] = 0;
		
		printf("Result=%s.\n", recognizer_final_result());
		
		strcat(resultBuffer, "{ \"text\" : \"");
		strcat(resultBuffer, recognizer_final_result());
		strcat(resultBuffer, "\" }");
		
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


