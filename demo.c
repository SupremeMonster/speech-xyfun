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

int awkeFlag = 0;	//����״̬flag��Ĭ��0δ���ѣ�1�ѻ���	
struct recorder* recorder;	//��ʼ��¼������
int record_state = MSP_AUDIO_SAMPLE_FIRST;	//��ʼ��¼��״̬
int ISR_STATUS = 0;//oneshotר�ã�������ʶ�����ʶ�����Ƿ��ѷ��ء�


typedef struct _UserData {
	int     build_fini;  //��ʶ�﷨�����Ƿ����
	int     update_fini; //��ʶ���´ʵ��Ƿ����
	int     errcode; //��¼�﷨��������´ʵ�ص�������
	char    grammar_id[MAX_GRAMMARID_LEN]; //�����﷨�������ص��﷨ID
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
	if (MSP_IVW_MSG_ERROR == msg) //���ѳ�����Ϣ
	{
		//printf("\n\nMSP_IVW_MSG_ERROR errCode = %d\n\n", param1);
		printf("����ʧ�ܣ�");
		awkeFlag = 0;
	}
	else if (MSP_IVW_MSG_WAKEUP == msg) //���ѳɹ���Ϣ
	{
		//printf("\n\nMSP_IVW_MSG_WAKEUP result = %s\n\n", info);
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 14);
		printf("�ɹ�������è���飡");
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 15);
		awkeFlag = 1;
	}

	return 0;	
}
//��ȡ¼�����ݵĺ���������ι������¼���ĺ���create_recorder
static void iat_cb(char* data, unsigned long len, void* user_para)
{
	int errcode;
	int ret = 0;
	const char* session_id = (const char*)user_para;//��ʼ������ʶ��ľ����
	//printf("����¼����ȡ");
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
//���л��ѵı���
void run_ivw(const char* session_begin_params)//���л��Ѳ���
{
	const char* session_id = NULL;
	int err_code = MSP_SUCCESS;
	WAVEFORMATEX wavfmt = DEFAULT_FORMAT;
	char sse_hints[128];	//�������ý���ʱ��ʾ����Ϣ
	int count = 0;

	//��ʼ����session
	session_id = QIVWSessionBegin(NULL, session_begin_params, &err_code);
	if (err_code != MSP_SUCCESS)
	{
		printf("QIVWSessionBegin failed! error code:%d\n", err_code);
		goto exit;
	}
	err_code = QIVWRegisterNotify(session_id, cb_ivw_msg_proc, NULL);	//Ϊ���ⶪʧ������ص�����Ӧ���ڴ˵���
	if (err_code != MSP_SUCCESS)
	{
		_snprintf(sse_hints, sizeof(sse_hints), "QIVWRegisterNotify errorCode=%d", err_code);
		printf("QIVWRegisterNotify failed! error code:%d\n", err_code);
		goto exit;
	}
	//��ʼ¼��
	err_code = create_recorder(&recorder, iat_cb, (void*)session_id);
	err_code = open_recorder(recorder, get_default_input_dev(), &wavfmt);
	err_code = start_record(recorder);

	//ѭ������������¼��״̬
	while (record_state != MSP_AUDIO_SAMPLE_LAST)
	{
		Sleep(200); //����ֱ�����ѽ������
		//printf("���ڼ���%d\n", record_state);
		if (awkeFlag == 1)
		{
			// awkeFlag = 0;	//�ָ���־λ�����´λ���
			break;			//����ѭ��
		}
		count++;
		if (count == 20)	//Ϊ�˷�ֹѭ������ʱд�뵽�����е����ݹ���
		{
			//���ͷŵ�ǰ¼����Դ
			stop_record(recorder);
			close_recorder(recorder);
			destroy_recorder(recorder);
			recorder = NULL;
			//printf("��ֹ��Ƶ��Դ�����ؽ�\n");
			//struct recorder *recorder;
			//�ؽ�¼����Դ
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
		QIVWSessionEnd(session_id, sse_hints); //����һ�λ��ѻỰ
	}	
}


// ��˷�ʶ��
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
	//���߻��ѵĲ�������
	const char* ssb_param = "ivw_threshold=0:1450,sst=wakeup,ivw_res_path =fo|res/ivw/wakeupresource.jet";	
	_snprintf(asr_params, MAX_PARAMS_LEN - 1,
		"sub = iat, domain = iat, language = zh_cn, accent = mandarin, sample_rate = 16000, result_type = plain, result_encoding = gb2312"		
	);	
	while (1) //��������
	{		
		if (awkeFlag!=1) {
			printf("�ȴ����ѣ�\n");
			run_ivw(ssb_param);	//���л��Ѻ���
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 12);
			printf("��ã���è��������������\n");
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 15);
		}
	    run_asr_mic(asr_params);	//������˷�����ʶ��
	}
	return 0;
}

int main(int argc, char* argv[])
{
	const char* login_config = "appid = 5ea928c9"; //��¼����������д���APPID
	UserData asr_data;
	int ret = 0;
	ret = MSPLogin(NULL, NULL, login_config); //��һ������Ϊ�û������ڶ�������Ϊ���룬��NULL���ɣ������������ǵ�¼����
	if (MSP_SUCCESS != ret) {
		printf("��¼ʧ�ܣ�%d\n", ret);
		goto exit;
	}
	memset(&asr_data, 0, sizeof(UserData));
	run_asr(&asr_data);		//ִ�з�װ�õ�����ʶ����	
	

exit:
	MSPLogout();
	printf("�밴������˳�...\n");
	_getch();
	return 0;
}
