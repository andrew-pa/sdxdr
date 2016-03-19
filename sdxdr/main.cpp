#include "dxut\dxut.h"

#include "dxut\cmmn.h"

//#define GAME_APP
#define TEST_APP

#ifdef TEST_APP
#include "test_app.h"
#define app test_app
#elif defined(GAME_APP)
#include "game_app.hpp"
#define app game_app
#endif

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	app a;
	a.Run(hInstance, nCmdShow);
}
