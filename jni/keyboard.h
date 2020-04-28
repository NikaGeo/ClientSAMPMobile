#pragma once

#include "vendor/RakNet/RakClientInterface.h"

#define MAX_LANG			3
#define LAYOUT_ENG			0
#define LAYOUT_NUM			1

#define LOWER_CASE			0
#define UPPER_CASE			1

#define KEY_DEFAULT			0
#define KEY_SHIFT			1
#define KEY_BACKSPACE		2
#define KEY_SWITCH			3
#define KEY_SPACE 			4
#define KEY_SEND			5

#define MAX_INPUT_LEN		0xBF

struct kbKey
{
	ImVec2 pos;
	ImVec2 symPos;
	float width;
	char code[2];
	char name[2][4];
	int type;
	int id;
};

typedef void keyboard_callback(const char* result);

class CKeyBoard
{
	friend class CGUI;
public:
	CKeyBoard();
	~CKeyBoard();

	void Open(keyboard_callback* handler);
	void Close();

	bool IsOpen() { return m_bEnable; }
	
	void InitENG();
	void InitNUM();
	kbKey* GetKeyFromPos(int x, int y);

	void HandleInput(kbKey &key);
	void AddCharToInput(char sym);
	void DeleteCharFromInput();
	void Send();

protected:
	void Render();
	bool OnTouchEvent(int type, bool multi, int x, int y);

public:
	bool m_bEnable;
	bool m_bInited;
	ImVec2 m_Size;
	ImVec2 m_Pos;
	float m_fKeySizeY;
	float m_fFontSize;

	int m_iLayout;
	int m_iCase;
	int m_iPushedKey;

	std::vector<kbKey> m_Rows[MAX_LANG][4]; // eng, rus, num

	std::string m_sInput;
	char m_utf8Input[MAX_INPUT_LEN*3 + 0xF];
	char m_pPassInputBuffer[4096];
	char* m_pPasswordSymbol;
	int m_iInputOffset;
	
	keyboard_callback *m_pHandler;
	
private:
	RakClientInterface* m_pRakClient;
};