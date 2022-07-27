
#include "vosk_api.h"

#include <stdio.h>

struct VoskModel
{
	int dummy;
};

struct VoskRecognizer
{
	int dummy;
};

static VoskModel dummyModel;
static VoskRecognizer dummyRecognizer;

static int silence_ctr = 0;

VoskModel *vosk_model_new(const char *model_path)
{
	printf("vosk_model_new, path=%s.\n", model_path);
	return &dummyModel;
}

void vosk_model_free(VoskModel *model)
{
	printf("vosk_model_free\n");
}

VoskRecognizer *vosk_recognizer_new(VoskModel *model, float sample_rate)
{
	printf("vosk_recognizer_new, sample_rate=%.2f.\n", sample_rate);
	return &dummyRecognizer;
}

void vosk_recognizer_free(VoskRecognizer *recognizer)
{
	printf("vosk_recognizer_free\n");
}

void vosk_recognizer_set_max_alternatives(VoskRecognizer *recognizer, int max_alternatives)
{
	printf("vosk_recognizer_set_max_alternatives, max_alternatives=%d.\n", max_alternatives);
}

void vosk_recognizer_set_words(VoskRecognizer *recognizer, int words)
{
	printf("vosk_recognizer_set_words, words=%d.\n", words);
}

int vosk_recognizer_accept_waveform(VoskRecognizer *recognizer, const char *data, int length)
{
	printf("vosk_recognizer_accept_waveform, length=%d.\n", length);
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
	printf("vosk_recognizer_result\n");
	return result_text;
}

const char *partial_result_text="{ \"partial\" : \"Partial result text\" }";

const char *vosk_recognizer_partial_result(VoskRecognizer *recognizer)
{
	printf("vosk_recognizer_partial_result\n");
	return partial_result_text;
}

const char *final_result_text="{ \"text\" : \"Final result text\" }";

const char *vosk_recognizer_final_result(VoskRecognizer *recognizer)
{
	printf("vosk_recognizer_final_result\n");
	return final_result_text;
}
