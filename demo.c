#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <conio.h>
#include <errno.h>
#include <process.h>

#include "include/msp_cmn.h"
#include "include/qivw.h"
#include "include/qisr.h"
#include "include/msp_errors.h"
#include "include/speech_recognizer.h"
#include "include/winrec.h"
#include "include/micro.h"
#include "include/tts.h"

int awkeFlag = 0;	//唤醒状态flag，默认0未唤醒，1已换醒	
struct recorder* recorder;	//初始化录音对象
int record_state = MSP_AUDIO_SAMPLE_FIRST;	//初始化录音状态
int ISR_STATUS = 0;//oneshot专用，用来标识命令词识别结果是否已返回。


typedef struct _UserData {
	int     build_fini;  //标识语法构建是否完成
	int     update_fini; //标识更新词典是否完成
	int     errcode; //记录语法构建或更新词典回调错误码
	char    grammar_id[MAX_GRAMMARID_LEN]; //保存语法构建返回的语法ID
}UserData;


#ifdef _WIN64
#pragma comment(lib,"libs/msc_x64.lib")
#else
#pragma comment(lib, "libs/msc.lib")
// #pragma comment(lib, "../../libs/tinyxml2.lib")
#endif


void wait_for_rec_stop(struct recorder* rec, unsigned int timeout_ms)
{
	while (!is_record_stopped(rec)) {
		Sleep(1);
		if (timeout_ms != (unsigned int)-1)
			if (0 == timeout_ms--)
				break;
	}
}

int cb_ivw_msg_proc(const char* sessionID, int msg, int param1, int param2, const void* info)
{
	if (MSP_IVW_MSG_ERROR == msg) //唤醒出错消息
	{
		//printf("\n\nMSP_IVW_MSG_ERROR errCode = %d\n\n", param1);
		printf("唤醒失败！");
		awkeFlag = 0;
	}
	else if (MSP_IVW_MSG_WAKEUP == msg) //唤醒成功消息
	{
		//printf("\n\nMSP_IVW_MSG_WAKEUP result = %s\n\n", info);
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 14);
		printf("成功唤醒天猫精灵！");
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 15);
		awkeFlag = 1;
	}

	return 0;	
}
//读取录音内容的函数，用于喂给创建录音的函数create_recorder
static void iat_cb(char* data, unsigned long len, void* user_para)
{
	int errcode;
	int ret = 0;
	const char* session_id = (const char*)user_para;//初始化本次识别的句柄。
	//printf("进入录音读取");
	if (len == 0 || data == NULL)
		return;

	errcode = QIVWAudioWrite(session_id, (const void*)data, len, record_state);

	if (MSP_SUCCESS != errcode)
	{
		printf("QIVWAudioWrite failed! error code:%d\n", errcode);
		ret = stop_record(recorder);
		if (ret != 0) {
			printf("Stop failed! \n");
			//return -E_SR_RECORDFAIL;
		}
		wait_for_rec_stop(recorder, (unsigned int)-1);
		QIVWAudioWrite(session_id, NULL, 0, MSP_AUDIO_SAMPLE_LAST);
		record_state = MSP_AUDIO_SAMPLE_LAST;
		//g_is_awaken_succeed = FALSE;
	}
	if (record_state == MSP_AUDIO_SAMPLE_FIRST) {
		record_state = MSP_AUDIO_SAMPLE_CONTINUE;
	}	
}
//运行唤醒的本体
void run_ivw(const char* session_begin_params)//运行唤醒步骤
{
	const char* session_id = NULL;
	int err_code = MSP_SUCCESS;
	WAVEFORMATEX wavfmt = DEFAULT_FORMAT;
	char sse_hints[128];	//用于设置结束时显示的信息
	int count = 0;

	//开始唤醒session
	session_id = QIVWSessionBegin(NULL, session_begin_params, &err_code);
	if (err_code != MSP_SUCCESS)
	{
		printf("QIVWSessionBegin failed! error code:%d\n", err_code);
		goto exit;
	}
	err_code = QIVWRegisterNotify(session_id, cb_ivw_msg_proc, NULL);	//为避免丢失句柄，回掉函数应当在此调用
	if (err_code != MSP_SUCCESS)
	{
		_snprintf(sse_hints, sizeof(sse_hints), "QIVWRegisterNotify errorCode=%d", err_code);
		printf("QIVWRegisterNotify failed! error code:%d\n", err_code);
		goto exit;
	}
	//开始录音
	err_code = create_recorder(&recorder, iat_cb, (void*)session_id);
	err_code = open_recorder(recorder, get_default_input_dev(), &wavfmt);
	err_code = start_record(recorder);

	//循环监听，保持录音状态
	while (record_state != MSP_AUDIO_SAMPLE_LAST)
	{
		Sleep(200); //阻塞直到唤醒结果出现
		//printf("正在监听%d\n", record_state);
		if (awkeFlag == 1)
		{
			// awkeFlag = 0;	//恢复标志位方便下次唤醒
			break;			//跳出循环
		}
		count++;
		if (count == 20)	//为了防止循环监听时写入到缓存中的数据过大
		{
			//先释放当前录音资源
			stop_record(recorder);
			close_recorder(recorder);
			destroy_recorder(recorder);
			recorder = NULL;
			//printf("防止音频资源过大，重建\n");
			//struct recorder *recorder;
			//重建录音资源
			err_code = create_recorder(&recorder, iat_cb, (void*)session_id);
			err_code = open_recorder(recorder, get_default_input_dev(), &wavfmt);
			err_code = start_record(recorder);
			count = 0;
		}
	}

exit:
	if (recorder) {
		if (!is_record_stopped(recorder))
			stop_record(recorder);
		close_recorder(recorder);
		destroy_recorder(recorder);
		recorder = NULL;
	}
	if (NULL != session_id)
	{
		QIVWSessionEnd(session_id, sse_hints); //结束一次唤醒会话
	}	
}


// 麦克风识别
static void run_asr_mic(const char* session_begin_params)
{
	int errcode;
	int i = 0;
	HANDLE helper_thread = NULL;

	struct speech_rec asr;
	DWORD waitres;
	char isquit = 0;
	struct speech_rec_notifier recnotifier = {
		on_result,
		on_speech_begin,
		on_speech_end
	};
	errcode = sr_init(&asr, session_begin_params, SR_MIC, DEFAULT_INPUT_DEVID, &recnotifier);
	if (errcode = sr_start_listening(&asr)) {
		printf("start listen failed %d\n", errcode);
		isquit = 1;
	}
	//Sleep(3000);
	
	if (errcode = sr_stop_listening(&asr)) {
		printf("stop listening failed %d\n", errcode);
		isquit = 1;
	}
	
	sr_stop_listening(&asr);

	sr_uninit(&asr);
}

int run_asr(UserData* udata)
{
	char asr_params[MAX_PARAMS_LEN] = { NULL };
	//离线唤醒的参数设置
	const char* ssb_param = "ivw_threshold=0:1450,sst=wakeup,ivw_res_path =fo|res/ivw/wakeupresource.jet";	
	_snprintf(asr_params, MAX_PARAMS_LEN - 1,
		"sub = iat, domain = iat, language = zh_cn, accent = mandarin, sample_rate = 16000, result_type = plain, result_encoding = gb2312"		
	);	
	while (1) //保持运行
	{		
		if (awkeFlag!=1) {
			printf("等待唤醒：\n");
			run_ivw(ssb_param);	//运行唤醒函数
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 12);
			printf("你好！天猫精灵正在听……\n");
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 15);
		}
	    run_asr_mic(asr_params);	//运行麦克风语音识别
	}
	return 0;
}

int main(int argc, char* argv[])
{
	const char* login_config = "appid = 5ea928c9"; //登录参数，这里写你的APPID
	UserData asr_data;
	int ret = 0;
	ret = MSPLogin(NULL, NULL, login_config); //第一个参数为用户名，第二个参数为密码，传NULL即可，第三个参数是登录参数
	if (MSP_SUCCESS != ret) {
		printf("登录失败：%d\n", ret);
		goto exit;
	}
	memset(&asr_data, 0, sizeof(UserData));
	run_asr(&asr_data);		//执行封装好的语音识别函数	
	

exit:
	MSPLogout();
	printf("请按任意键退出...\n");
	_getch();
	return 0;
}
