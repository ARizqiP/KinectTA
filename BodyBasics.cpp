//------------------------------------------------------------------------------
// <copyright file="BodyBasics.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

#include "stdafx.h"
#include <strsafe.h>
#include "resource.h"
#include "BodyBasics.h"
#include <cmath>
#include <string>
#include <iostream>
#include <dwrite.h>

static const float c_JointThickness = 3.0f;
static const float c_TrackedBoneThickness = 6.0f;
static const float c_InferredBoneThickness = 1.0f;
static const float c_HandSize = 30.0f;
static const float PI = 3.1415927;
static WCHAR* semaphoreDictionary [8][8] = {
	{ L"-", L"A", L"B", L"C", L"D", L"E", L"F", L"G" },
	{ L"A", L"", L"H", L"I", L"K", L"L", L"M", L"N" },
	{ L"B", L"H", L"", L"O", L"P", L"Q", L"R", L"S" },
	{ L"C", L"I", L"O", L"", L"T", L"U", L"Y", L"!" },
	{ L"D", L"K", L"P", L"T", L"", L"#", L"J", L"V" },
	{ L"E", L"L", L"Q", L"U", L"#", L"", L"W", L"X" },
	{ L"F", L"M", L"R", L"Y", L"J", L"W", L"", L"Z" },
	{ L"G", L"N", L"S", L"!", L"V", L"X", L"Z", L"" }
};
WCHAR* code [6];
WCHAR* tempCode[6];
bool inRest[6];
UINT64 timeInLetter[6];
int trackedBody = -1, leftArmCode = -1, rightArmCode = -1;
float leftAngle, rightAngle;
float vectorAngle(D2D_VECTOR_2F , D2D_VECTOR_2F );
float vectorAngle(D2D_VECTOR_3F , D2D_VECTOR_3F );
float dotProduct(D2D_VECTOR_2F, D2D_VECTOR_2F);
float determinant(D2D_VECTOR_2F , D2D_VECTOR_2F );
float dotProduct(D2D_VECTOR_3F , D2D_VECTOR_3F );
float vectorLength(float , float , float );
float vectorLength(float, float);
float vectorLength(D2D_VECTOR_2F );
float vectorLength(D2D_VECTOR_3F);

IDWriteFactory* m_pDWriteFactory;
IDWriteTextFormat* m_pTextFormat;

/// <summary>
/// Entry point for the application
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="hPrevInstance">always 0</param>
/// <param name="lpCmdLine">command line arguments</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
/// <returns>status</returns>
int APIENTRY wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd
	)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	CBodyBasics application;
	application.Run(hInstance, nShowCmd);
}

/// <summary>
/// Constructor
/// </summary>
CBodyBasics::CBodyBasics() :
m_hWnd(NULL),
m_nStartTime(0),
m_nLastCounter(0),
m_nFramesSinceUpdate(0),
m_fFreq(0),
m_nNextStatusTime(0LL),
m_pKinectSensor(NULL),
m_pCoordinateMapper(NULL),
m_pBodyFrameReader(NULL),
m_pD2DFactory(NULL),
m_pRenderTarget(NULL),
m_pBrushJointTracked(NULL),
m_pBrushJointInferred(NULL),
m_pBrushBoneTracked(NULL),
m_pBrushBoneInferred(NULL),
m_pBrushHandClosed(NULL),
m_pBrushHandOpen(NULL),
m_pOutputRGBX(NULL),
m_pBackgroundRGBX(NULL),
m_pColorRGBX(NULL),
m_pBrushHandLasso(NULL)
{
	LARGE_INTEGER qpf = { 0 };
	if (QueryPerformanceFrequency(&qpf))
	{
		m_fFreq = double(qpf.QuadPart);
	}
}


/// <summary>
/// Destructor
/// </summary>
CBodyBasics::~CBodyBasics()
{
	DiscardDirect2DResources();

	// clean up Direct2D
	SafeRelease(m_pD2DFactory);

	// done with body frame reader
	SafeRelease(m_pBodyFrameReader);

	// done with coordinate mapper
	SafeRelease(m_pCoordinateMapper);

	// close the Kinect Sensor
	if (m_pKinectSensor)
	{
		m_pKinectSensor->Close();
	}

	SafeRelease(m_pKinectSensor);
}

/// <summary>
/// Creates the main window and begins processing
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
int CBodyBasics::Run(HINSTANCE hInstance, int nCmdShow)
{
	MSG       msg = { 0 };
	WNDCLASS  wc;

	// Dialog custom window class
	ZeroMemory(&wc, sizeof(wc));
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP));
	wc.lpfnWndProc = DefDlgProcW;
	wc.lpszClassName = L"BodyBasicsAppDlgWndClass";

	if (!RegisterClassW(&wc))
	{
		return 0;
	}

	// Create main application window
	HWND hWndApp = CreateDialogParamW(
		NULL,
		MAKEINTRESOURCE(IDD_APP),
		NULL,
		(DLGPROC)CBodyBasics::MessageRouter,
		reinterpret_cast<LPARAM>(this));

	// Show window
	ShowWindow(hWndApp, nCmdShow);

	// Main message loop
	while (WM_QUIT != msg.message)
	{
		Update();

		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			// If a dialog message will be taken care of by the dialog proc
			if (hWndApp && IsDialogMessageW(hWndApp, &msg))
			{
				continue;
			}

			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	return static_cast<int>(msg.wParam);
}

/// <summary>
/// Main processing function
/// </summary>
void CBodyBasics::Update()
{
	if (!m_pBodyFrameReader)
	{
		return;
	}

	IBodyFrame* pBodyFrame = NULL;

	HRESULT hr = m_pBodyFrameReader->AcquireLatestFrame(&pBodyFrame);

	if (SUCCEEDED(hr))
	{
		INT64 nTime = 0;

		hr = pBodyFrame->get_RelativeTime(&nTime);

		IBody* ppBodies[BODY_COUNT] = { 0 };

		if (SUCCEEDED(hr))
		{
			hr = pBodyFrame->GetAndRefreshBodyData(_countof(ppBodies), ppBodies);
		}

		if (SUCCEEDED(hr))
		{
			ProcessBody(nTime, BODY_COUNT, ppBodies);
		}

		for (int i = 0; i < _countof(ppBodies); ++i)
		{
			SafeRelease(ppBodies[i]);
		}
	}

	SafeRelease(pBodyFrame);
}

/// <summary>
/// Handles window messages, passes most to the class instance to handle
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CBodyBasics::MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CBodyBasics* pThis = NULL;

	if (WM_INITDIALOG == uMsg)
	{
		pThis = reinterpret_cast<CBodyBasics*>(lParam);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
	}
	else
	{
		pThis = reinterpret_cast<CBodyBasics*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}

	if (pThis)
	{
		return pThis->DlgProc(hWnd, uMsg, wParam, lParam);
	}

	return 0;
}

/// <summary>
/// Handle windows messages for the class instance
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CBodyBasics::DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	UNREFERENCED_PARAMETER(lParam);

	switch (message)
	{
	case WM_INITDIALOG:
	{
		// Bind application window handle
		m_hWnd = hWnd;

		// Init Direct2D
		HRESULT hr;
		hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);

		if (SUCCEEDED(hr))
		{

			// Create a DirectWrite factory.
			hr = DWriteCreateFactory(
				DWRITE_FACTORY_TYPE_SHARED,
				__uuidof(m_pDWriteFactory),
				reinterpret_cast<IUnknown **>(&m_pDWriteFactory)
				);
		}
		if (SUCCEEDED(hr))
		{
			// Create a DirectWrite text format object.
			hr = m_pDWriteFactory->CreateTextFormat(
				L"Arial Unicode MS",
				NULL,
				DWRITE_FONT_WEIGHT_NORMAL,
				DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL,
				50,
				L"", //locale
				&m_pTextFormat
				);
		}
		if (SUCCEEDED(hr))
		{
			// Center the text horizontally and vertically.
			m_pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

			m_pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);


		}

		// Get and initialize the default Kinect sensor
		InitializeDefaultSensor();
	}
	break;

	// If the titlebar X is clicked, destroy app
	case WM_CLOSE:
		DestroyWindow(hWnd);
		break;

	case WM_DESTROY:
		// Quit the main message pump
		PostQuitMessage(0);
		break;
	}

	return FALSE;
}

/// <summary>
/// Initializes the default Kinect sensor
/// </summary>
/// <returns>indicates success or failure</returns>
HRESULT CBodyBasics::InitializeDefaultSensor()
{
	HRESULT hr;

	hr = GetDefaultKinectSensor(&m_pKinectSensor);
	if (FAILED(hr))
	{
		return hr;
	}

	if (m_pKinectSensor)
	{
		// Initialize the Kinect and get coordinate mapper and the body reader
		IBodyFrameSource* pBodyFrameSource = NULL;

		hr = m_pKinectSensor->Open();

		if (SUCCEEDED(hr))
		{
			hr = m_pKinectSensor->get_CoordinateMapper(&m_pCoordinateMapper);
		}

		if (SUCCEEDED(hr))
		{
			hr = m_pKinectSensor->get_BodyFrameSource(&pBodyFrameSource);
		}

		if (SUCCEEDED(hr))
		{
			hr = pBodyFrameSource->OpenReader(&m_pBodyFrameReader);
		}

		SafeRelease(pBodyFrameSource);
	}

	if (!m_pKinectSensor || FAILED(hr))
	{
		SetStatusMessage(L"No ready Kinect found!", 10000, true);
		return E_FAIL;
	}

	return hr;
}

/// <summary>
/// Handle new body data
/// <param name="nTime">timestamp of frame</param>
/// <param name="nBodyCount">body data count</param>
/// <param name="ppBodies">body data in frame</param>
/// </summary>
void CBodyBasics::ProcessBody(INT64 nTime, int nBodyCount, IBody** ppBodies)
{
	if (m_hWnd)
	{
		HRESULT hr = EnsureDirect2DResources();

		if (SUCCEEDED(hr) && m_pRenderTarget && m_pCoordinateMapper)
		{
			m_pRenderTarget->BeginDraw();
			m_pRenderTarget->Clear();

			RECT rct;
			GetClientRect(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), &rct);
			int width = rct.right;
			int height = rct.bottom;

			for (int i = 0; i < nBodyCount; ++i)
			{
				IBody* pBody = ppBodies[i];
				if (pBody)
				{
					BOOLEAN bTracked = false;
					hr = pBody->get_IsTracked(&bTracked);

					if (SUCCEEDED(hr) && bTracked)
					{
						Joint joints[JointType_Count];
						D2D1_POINT_2F jointPoints[JointType_Count];
						HandState leftHandState = HandState_Unknown;
						HandState rightHandState = HandState_Unknown;

						pBody->get_HandLeftState(&leftHandState);
						pBody->get_HandRightState(&rightHandState);

						hr = pBody->GetJoints(_countof(joints), joints);
						if (SUCCEEDED(hr))
						{
							for (int j = 0; j < _countof(joints); ++j)
							{
								jointPoints[j] = BodyToScreen(joints[j].Position, width, height);
							}

							processSemaphore(joints, jointPoints, i, nTime);

							DrawBody(joints, jointPoints);

							DrawHand(leftHandState, jointPoints[JointType_HandLeft]);
							DrawHand(rightHandState, jointPoints[JointType_HandRight]);

							//break;
						}
					}
				}
			}

			hr = m_pRenderTarget->EndDraw();

			// Device lost, need to recreate the render target
			// We'll dispose it now and retry drawing
			if (D2DERR_RECREATE_TARGET == hr)
			{
				hr = S_OK;
				DiscardDirect2DResources();
			}
		}

		if (!m_nStartTime)
		{
			m_nStartTime = nTime;
		}

		double fps = 0.0;

		LARGE_INTEGER qpcNow = { 0 };
		if (m_fFreq)
		{
			if (QueryPerformanceCounter(&qpcNow))
			{
				if (m_nLastCounter)
				{
					m_nFramesSinceUpdate++;
					fps = m_fFreq * m_nFramesSinceUpdate / double(qpcNow.QuadPart - m_nLastCounter);
				}
			}
		}

		WCHAR szStatusMessage[64];

		StringCchPrintf(szStatusMessage, _countof(szStatusMessage), L" FPS = %0.2f    Time = %I64d", fps, (nTime - m_nStartTime));

		if (SetStatusMessage(szStatusMessage, 1000, false))
		{
			m_nLastCounter = qpcNow.QuadPart;
			m_nFramesSinceUpdate = 0;
		}
	}
}

/// <summary>
/// Set the status bar message
/// </summary>
/// <param name="szMessage">message to display</param>
/// <param name="showTimeMsec">time in milliseconds to ignore future status messages</param>
/// <param name="bForce">force status update</param>
bool CBodyBasics::SetStatusMessage(_In_z_ WCHAR* szMessage, DWORD nShowTimeMsec, bool bForce)
{
	INT64 now = GetTickCount64();

	if (m_hWnd && (bForce || (m_nNextStatusTime <= now)))
	{
		SetDlgItemText(m_hWnd, IDC_STATUS, szMessage);
		m_nNextStatusTime = now + nShowTimeMsec;

		return true;
	}

	return false;
}

/// <summary>
/// Ensure necessary Direct2d resources are created
/// </summary>
/// <returns>S_OK if successful, otherwise an error code</returns>
HRESULT CBodyBasics::EnsureDirect2DResources()
{
	HRESULT hr = S_OK;

	if (m_pD2DFactory && !m_pRenderTarget)
	{
		RECT rc;
		GetWindowRect(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), &rc);

		int width = rc.right - rc.left;
		int height = rc.bottom - rc.top;
		D2D1_SIZE_U size = D2D1::SizeU(width, height);
		D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
		rtProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
		rtProps.usage = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;

		// Create a Hwnd render target, in order to render to the window set in initialize
		hr = m_pD2DFactory->CreateHwndRenderTarget(
			rtProps,
			D2D1::HwndRenderTargetProperties(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), size),
			&m_pRenderTarget
			);

		if (FAILED(hr))
		{
			SetStatusMessage(L"Couldn't create Direct2D render target!", 10000, true);
			return hr;
		}

		// light green
		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.27f, 0.75f, 0.27f), &m_pBrushJointTracked);

		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Yellow, 1.0f), &m_pBrushJointInferred);
		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Green, 1.0f), &m_pBrushBoneTracked);
		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray, 1.0f), &m_pBrushBoneInferred);

		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red, 0.5f), &m_pBrushHandClosed);
		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Green, 0.5f), &m_pBrushHandOpen);
		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Blue, 0.5f), &m_pBrushHandLasso);

	}

	return hr;
}

/// <summary>
/// Dispose Direct2d resources 
/// </summary>
void CBodyBasics::DiscardDirect2DResources()
{
	SafeRelease(m_pRenderTarget);

	SafeRelease(m_pBrushJointTracked);
	SafeRelease(m_pBrushJointInferred);
	SafeRelease(m_pBrushBoneTracked);
	SafeRelease(m_pBrushBoneInferred);

	SafeRelease(m_pBrushHandClosed);
	SafeRelease(m_pBrushHandOpen);
	SafeRelease(m_pBrushHandLasso);
}

/// <summary>
/// Converts a body point to screen space
/// </summary>
/// <param name="bodyPoint">body point to tranform</param>
/// <param name="width">width (in pixels) of output buffer</param>
/// <param name="height">height (in pixels) of output buffer</param>
/// <returns>point in screen-space</returns>
D2D1_POINT_2F CBodyBasics::BodyToScreen(const CameraSpacePoint& bodyPoint, int width, int height)
{
	// Calculate the body's position on the screen
	DepthSpacePoint depthPoint = { 0 };
	m_pCoordinateMapper->MapCameraPointToDepthSpace(bodyPoint, &depthPoint);

	float screenPointX = static_cast<float>(depthPoint.X * width) / cDepthWidth;
	float screenPointY = static_cast<float>(depthPoint.Y * height) / cDepthHeight;

	return D2D1::Point2F(screenPointX, screenPointY);
}


/// <summary>
/// Convert arm states to letter according to flag semaphore signals.
/// </summary>
/// <param name="pJoints">joint data</param>
/// <param name="pJointPoints">joint positions converted to screen space</param>
/// <param name="nTime">timestamp of frame</param>
void CBodyBasics::processSemaphore(const Joint* pJoints, const D2D1_POINT_2F* pJointPoints, int bodyIndex, UINT64 nTime){
	leftArmCode = -1;
	rightArmCode = -1;
	D2D_VECTOR_2F baseVector = {
		0,
		-1, };
	//Get arm state
	D2D_VECTOR_3F leftUpperArm3DVector{
		pJoints[JointType_ElbowLeft].Position.X - pJoints[JointType_ShoulderLeft].Position.X,
		pJoints[JointType_ElbowLeft].Position.Y - pJoints[JointType_ShoulderLeft].Position.Y,
		pJoints[JointType_ElbowLeft].Position.Z - pJoints[JointType_ShoulderLeft].Position.Z, };
	D2D_VECTOR_3F leftLowerArm3DVector{
		pJoints[JointType_WristLeft].Position.X - pJoints[JointType_ElbowLeft].Position.X,
		pJoints[JointType_WristLeft].Position.Y - pJoints[JointType_ElbowLeft].Position.Y,
		pJoints[JointType_WristLeft].Position.Z - pJoints[JointType_ElbowLeft].Position.Z, };
	D2D_VECTOR_3F rightUpperArm3DVector{
		pJoints[JointType_ElbowRight].Position.X - pJoints[JointType_ShoulderRight].Position.X,
		pJoints[JointType_ElbowRight].Position.Y - pJoints[JointType_ShoulderRight].Position.Y,
		pJoints[JointType_ElbowRight].Position.Z - pJoints[JointType_ShoulderRight].Position.Z, };
	D2D_VECTOR_3F rightLowerArm3DVector{
		pJoints[JointType_WristRight].Position.X - pJoints[JointType_ElbowRight].Position.X,
		pJoints[JointType_WristRight].Position.Y - pJoints[JointType_ElbowRight].Position.Y,
		pJoints[JointType_WristRight].Position.Z - pJoints[JointType_ElbowRight].Position.Z, };
	//Calculate arm position code
	int margin = 10;
	if (abs(pJoints[JointType_ElbowLeft].Position.Z - pJoints[JointType_ShoulderLeft].Position.Z) <= vectorLength(leftUpperArm3DVector)){
		D2D_VECTOR_2F leftArmScreenVector{
			pJointPoints[JointType_WristLeft].x - pJointPoints[JointType_ShoulderLeft].x,
			pJointPoints[JointType_WristLeft].y - pJointPoints[JointType_ShoulderLeft].y, };
		leftAngle = 180+(180 * atan2(determinant(leftArmScreenVector, baseVector), dotProduct(leftArmScreenVector, baseVector))) / PI;
		if (leftAngle <= 0 + margin || leftAngle >= 360 - margin)leftArmCode = 0;
		else if (leftAngle >= 45 - margin && leftAngle <= 45 + margin)leftArmCode = 1;
		else if (leftAngle >= 90 - margin && leftAngle <= 90 + margin)leftArmCode = 2;
		else if (leftAngle >= 135 - margin && leftAngle <= 135 + margin)leftArmCode = 3;
		else if (leftAngle >= 180 - margin && leftAngle <= 180 + margin)leftArmCode = 4;
		else if (leftAngle >= 225 - margin && leftAngle <= 225 + margin)leftArmCode = 5;
		else if (leftAngle >= 270 - margin && leftAngle <= 270 + margin)leftArmCode = 6;
		else if (leftAngle >= 315 - margin && leftAngle <= 315 + margin)leftArmCode = 7;
	}
	if (abs(pJoints[JointType_ElbowRight].Position.Z - pJoints[JointType_ShoulderRight].Position.Z) <= vectorLength(rightUpperArm3DVector)){
		D2D_VECTOR_2F rightArmScreenVector{
			pJointPoints[JointType_WristRight].x - pJointPoints[JointType_ShoulderRight].x,
			pJointPoints[JointType_WristRight].y - pJointPoints[JointType_ShoulderRight].y, };
		rightAngle = 180 + (180 * atan2(determinant(rightArmScreenVector, baseVector), dotProduct(rightArmScreenVector, baseVector))) / PI;
		if (rightAngle <= 0 + margin || rightAngle >= 360 - margin)rightArmCode = 0;
		else if (rightAngle >= 45 - margin && rightAngle <= 45 + margin)rightArmCode = 1;
		else if (rightAngle >= 90 - margin && rightAngle <= 90 + margin)rightArmCode = 2;
		else if (rightAngle >= 135 - margin && rightAngle <= 135 + margin)rightArmCode = 3;
		else if (rightAngle >= 180 - margin && rightAngle <= 180 + margin)rightArmCode = 4;
		else if (rightAngle >= 225 - margin && rightAngle <= 225 + margin)rightArmCode = 5;
		else if (rightAngle >= 270 - margin && rightAngle <= 270 + margin)rightArmCode = 6;
		else if (rightAngle >= 315 - margin && rightAngle <= 315 + margin)rightArmCode = 7;
	}

	//Different action for in rest or not
	if (leftArmCode == 0 && rightArmCode == 0){
		inRest[bodyIndex] = true;
		//baca rest
		tempCode[bodyIndex] = semaphoreDictionary[leftArmCode][rightArmCode];
		code[bodyIndex] = tempCode[bodyIndex];
	}
	else if (code[bodyIndex] == semaphoreDictionary[0][0]){
		//jika baru baca
		if (inRest[bodyIndex]){
			inRest[bodyIndex] = false;
			timeInLetter[bodyIndex] = nTime;
		}
		else{
			//jika berganti, reset
			if (tempCode[bodyIndex] != semaphoreDictionary[leftArmCode][rightArmCode]){
				timeInLetter[bodyIndex] = nTime;
			}
			tempCode[bodyIndex] = semaphoreDictionary[leftArmCode][rightArmCode];
			//jika sudah sekian detik
			if (nTime - timeInLetter[bodyIndex] >= 20000000){
				//tetapkan
				code[bodyIndex] = tempCode[bodyIndex];
			}
			//jika tidak
			else{

			}			
		}
	}

	//Convert position code to letter
	//code[bodyIndex] = semaphoreDictionary[leftArmCode][rightArmCode];

	//And draw the letter
	m_pRenderTarget->DrawTextW(
		code[bodyIndex],
		1,
		m_pTextFormat,
		D2D1::RectF(pJointPoints[JointType_Head].x - 50, pJointPoints[JointType_Head].y - 100, pJointPoints[JointType_Head].x + 50, pJointPoints[JointType_Head].y),
		m_pBrushHandClosed
		);
}

float vectorAngle(D2D_VECTOR_2F a, D2D_VECTOR_2F b){
	return cos(dotProduct(a, b) / (vectorLength(a)*vectorLength(b)));
}

float vectorAngle(D2D_VECTOR_3F a, D2D_VECTOR_3F b){
	return cos(dotProduct(a, b) / (vectorLength(a)*vectorLength(b)));
}

float dotProduct(D2D_VECTOR_2F a, D2D_VECTOR_2F b){
	return a.x*b.x + a.y*b.y;
}

float determinant(D2D_VECTOR_2F a, D2D_VECTOR_2F b){
	return a.x*b.y - a.y*b.x;
}

float dotProduct(D2D_VECTOR_3F a, D2D_VECTOR_3F b){
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

float vectorLength(float x, float y, float z){
	return sqrtf(x*x + y*y + z*z);
}

float vectorLength(float x, float y){
	return vectorLength(x, y, 0);
}

float vectorLength(D2D_VECTOR_2F a){
	return vectorLength(a.x, a.y);
}

float vectorLength(D2D_VECTOR_3F a){
	return vectorLength(a.x, a.y, a.z);
}

/// <summary>
/// Draws a body 
/// </summary>
/// <param name="pJoints">joint data</param>
/// <param name="pJointPoints">joint positions converted to screen space</param>
void CBodyBasics::DrawBody(const Joint* pJoints, const D2D1_POINT_2F* pJointPoints)
{
	// Draw the bones

	// Torso
	DrawBone(pJoints, pJointPoints, JointType_Head, JointType_Neck);
	DrawBone(pJoints, pJointPoints, JointType_Neck, JointType_SpineShoulder);
	DrawBone(pJoints, pJointPoints, JointType_SpineShoulder, JointType_SpineMid);
	DrawBone(pJoints, pJointPoints, JointType_SpineMid, JointType_SpineBase);
	DrawBone(pJoints, pJointPoints, JointType_SpineShoulder, JointType_ShoulderRight);
	DrawBone(pJoints, pJointPoints, JointType_SpineShoulder, JointType_ShoulderLeft);
	DrawBone(pJoints, pJointPoints, JointType_SpineBase, JointType_HipRight);
	DrawBone(pJoints, pJointPoints, JointType_SpineBase, JointType_HipLeft);

	// Right Arm    
	DrawBone(pJoints, pJointPoints, JointType_ShoulderRight, JointType_ElbowRight);
	DrawBone(pJoints, pJointPoints, JointType_ElbowRight, JointType_WristRight);
	DrawBone(pJoints, pJointPoints, JointType_WristRight, JointType_HandRight);
	DrawBone(pJoints, pJointPoints, JointType_HandRight, JointType_HandTipRight);
	DrawBone(pJoints, pJointPoints, JointType_WristRight, JointType_ThumbRight);

	// Left Arm
	DrawBone(pJoints, pJointPoints, JointType_ShoulderLeft, JointType_ElbowLeft);
	DrawBone(pJoints, pJointPoints, JointType_ElbowLeft, JointType_WristLeft);
	DrawBone(pJoints, pJointPoints, JointType_WristLeft, JointType_HandLeft);
	DrawBone(pJoints, pJointPoints, JointType_HandLeft, JointType_HandTipLeft);
	DrawBone(pJoints, pJointPoints, JointType_WristLeft, JointType_ThumbLeft);

	// Right Leg
	DrawBone(pJoints, pJointPoints, JointType_HipRight, JointType_KneeRight);
	DrawBone(pJoints, pJointPoints, JointType_KneeRight, JointType_AnkleRight);
	DrawBone(pJoints, pJointPoints, JointType_AnkleRight, JointType_FootRight);

	// Left Leg
	DrawBone(pJoints, pJointPoints, JointType_HipLeft, JointType_KneeLeft);
	DrawBone(pJoints, pJointPoints, JointType_KneeLeft, JointType_AnkleLeft);
	DrawBone(pJoints, pJointPoints, JointType_AnkleLeft, JointType_FootLeft);

	// Draw the joints
	for (int i = 0; i < JointType_Count; ++i)
	{
		D2D1_ELLIPSE ellipse = D2D1::Ellipse(pJointPoints[i], c_JointThickness, c_JointThickness);

		if (pJoints[i].TrackingState == TrackingState_Inferred)
		{
			m_pRenderTarget->FillEllipse(ellipse, m_pBrushJointInferred);
		}
		else if (pJoints[i].TrackingState == TrackingState_Tracked)
		{
			m_pRenderTarget->FillEllipse(ellipse, m_pBrushJointTracked);
		}
	}
}

/// <summary>
/// Draws one bone of a body (joint to joint)
/// </summary>
/// <param name="pJoints">joint data</param>
/// <param name="pJointPoints">joint positions converted to screen space</param>
/// <param name="pJointPoints">joint positions converted to screen space</param>
/// <param name="joint0">one joint of the bone to draw</param>
/// <param name="joint1">other joint of the bone to draw</param>
void CBodyBasics::DrawBone(const Joint* pJoints, const D2D1_POINT_2F* pJointPoints, JointType joint0, JointType joint1)
{
	TrackingState joint0State = pJoints[joint0].TrackingState;
	TrackingState joint1State = pJoints[joint1].TrackingState;

	// If we can't find either of these joints, exit
	if ((joint0State == TrackingState_NotTracked) || (joint1State == TrackingState_NotTracked))
	{
		return;
	}

	// Don't draw if both points are inferred
	if ((joint0State == TrackingState_Inferred) && (joint1State == TrackingState_Inferred))
	{
		return;
	}

	// We assume all drawn bones are inferred unless BOTH joints are tracked
	if ((joint0State == TrackingState_Tracked) && (joint1State == TrackingState_Tracked))
	{
		m_pRenderTarget->DrawLine(pJointPoints[joint0], pJointPoints[joint1], m_pBrushBoneTracked, c_TrackedBoneThickness);
	}
	else
	{
		m_pRenderTarget->DrawLine(pJointPoints[joint0], pJointPoints[joint1], m_pBrushBoneInferred, c_InferredBoneThickness);
	}
}

/// <summary>
/// Draws a hand symbol if the hand is tracked: red circle = closed, green circle = opened; blue circle = lasso
/// </summary>
/// <param name="handState">state of the hand</param>
/// <param name="handPosition">position of the hand</param>
void CBodyBasics::DrawHand(HandState handState, const D2D1_POINT_2F& handPosition)
{
	D2D1_ELLIPSE ellipse = D2D1::Ellipse(handPosition, c_HandSize, c_HandSize);

	switch (handState)
	{
	case HandState_Closed:
		m_pRenderTarget->FillEllipse(ellipse, m_pBrushHandClosed);
		break;

	case HandState_Open:
		m_pRenderTarget->FillEllipse(ellipse, m_pBrushHandOpen);
		break;

	case HandState_Lasso:
		m_pRenderTarget->FillEllipse(ellipse, m_pBrushHandLasso);
		break;
	}
}
