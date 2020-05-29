#pragma once
/*
 麦克风相关方法封装的模块
 @author		yding
 @date		2020/05/28
*/
#define	BUFFER_SIZE	4096
#define SAMPLE_RATE_16K     (16000)
#define SAMPLE_RATE_8K      (8000)
#define MAX_GRAMMARID_LEN   (32)
#define MAX_PARAMS_LEN      (1024)



void on_result(const char* result, char is_last);
void on_speech_begin();
void on_speech_end(int reason);