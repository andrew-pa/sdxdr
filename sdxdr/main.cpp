#include "dxut\dxut.h"

#include "dxut\cmmn.h"

#include "test_app.h"


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	test_app a;
	a.Run(hInstance, nCmdShow);
}
