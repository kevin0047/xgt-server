﻿// 0430MFCAppDlg.cpp: 구현 파일
#include "pch.h"
#include "framework.h"
#include "0430MFCApp.h"
#include "0430MFCAppDlg.h"
#include "afxdialogex.h"
#include <fstream>
#include <sstream>
#include <atlconv.h>
#include <Shlwapi.h>  
#pragma comment(lib, "Shlwapi.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// ModBus TCP 함수 코드
#define MODBUS_FC_READ_HOLDING_REGISTERS    0x03

// ModBus TCP 트랜잭션 ID
static WORD g_transactionId = 0;

// CModbusTcpSocket 클래스 구현
CModbusTcpSocket::CModbusTcpSocket()
    : m_pParent(NULL), m_nSocketID(0), m_bConnected(false)
{
}

CModbusTcpSocket::~CModbusTcpSocket()
{
    if (m_hSocket != INVALID_SOCKET)
        Close();
}

void CModbusTcpSocket::OnConnect(int nErrorCode)
{
    m_bConnected = (nErrorCode == 0);
    m_pParent->PostMessage(WM_SOCKET_CONNECT, m_nSocketID, nErrorCode);
    CAsyncSocket::OnConnect(nErrorCode);
}

void CModbusTcpSocket::OnReceive(int nErrorCode)
{
    if (nErrorCode == 0)
    {
        BYTE buffer[4096];
        int nRead = Receive(buffer, sizeof(buffer));
        if (nRead > 0)
        {
            // 버퍼에 데이터 추가
            m_RecvBuffer.insert(m_RecvBuffer.end(), buffer, buffer + nRead);

            // 데이터 처리를 위해 부모에게 알림
            m_pParent->PostMessage(WM_SOCKET_RECEIVE, m_nSocketID, m_RecvBuffer.size());
        }
    }
    CAsyncSocket::OnReceive(nErrorCode);
}

void CModbusTcpSocket::OnClose(int nErrorCode)
{

    m_bConnected = false;
    m_pParent->PostMessage(WM_SOCKET_CLOSE, m_nSocketID, nErrorCode);
    CAsyncSocket::OnClose(nErrorCode);
}

void CModbusTcpSocket::OnSend(int nErrorCode)
{
    CAsyncSocket::OnSend(nErrorCode);
}

BOOL CModbusTcpSocket::SendData(const BYTE* pData, int nLength)
{
    if (!m_bConnected || m_hSocket == INVALID_SOCKET)
        return FALSE;

    int nSent = Send(pData, nLength);
    return (nSent == nLength);
}

// CMy0430MFCAppDlg 대화 상자
CMy0430MFCAppDlg::CMy0430MFCAppDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_MY0430MFCAPP_DIALOG, pParent)
    , m_delayMs(20) 
    , m_bPolling(false)
    , m_timerId(0)
    , m_indicatorCount(0)
    , m_strPLCIP(_T("192.168.250.111"))  // PLC IP 초기화
    , m_nPLCPort(2004)       // PLC 포트 초기화
    , m_strLogFilePath(_T(""))  // 로그 파일 경로 초기화
    , m_plcHeartbeatTimerId(0)        // 추가
    , m_bHeartbeatValue(false)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

CMy0430MFCAppDlg::~CMy0430MFCAppDlg()
{
    if (m_timerId != 0)
        KillTimer(m_timerId);

    if (m_plcHeartbeatTimerId != 0)   // 추가
        KillTimer(m_plcHeartbeatTimerId);

    for (size_t i = 0; i < m_indicatorCount; i++) {
        if (m_indicators[i].connected)
            DisconnectIndicator(i);
    }
}

void CMy0430MFCAppDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST_INDICATORS, m_listIndicators);
    DDX_Control(pDX, IDC_EDIT_DELAY, m_editDelay);
    DDX_Control(pDX, IDC_BUTTON_CONNECT, m_btnConnect);
    DDX_Control(pDX, IDC_BUTTON_DISCONNECT, m_btnDisconnect);
    DDX_Control(pDX, IDC_BUTTON_START_POLLING, m_btnStartPolling);
    DDX_Control(pDX, IDC_BUTTON_STOP_POLLING, m_btnStopPolling);
    DDX_Control(pDX, IDC_LIST_LOG, m_logList);
    DDX_Text(pDX, IDC_EDIT_DELAY, m_delayMs);
    DDX_Text(pDX, IDC_EDIT_PLC_IP, m_strPLCIP);
    DDX_Text(pDX, IDC_EDIT_PLC_PORT, m_nPLCPort);
    DDX_Control(pDX, IDC_EDIT_PLC_IP, m_editPLCIP);
    DDX_Control(pDX, IDC_EDIT_PLC_PORT, m_editPLCPort);
    DDX_Control(pDX, IDC_STATIC_PLC_STATUS, m_staticPLCStatus);
}

BEGIN_MESSAGE_MAP(CMy0430MFCAppDlg, CDialogEx)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_BN_CLICKED(IDC_BUTTON_CONNECT, &CMy0430MFCAppDlg::OnBnClickedButtonConnect)
    ON_BN_CLICKED(IDC_BUTTON_DISCONNECT, &CMy0430MFCAppDlg::OnBnClickedButtonDisconnect)
    ON_BN_CLICKED(IDC_BUTTON_START_POLLING, &CMy0430MFCAppDlg::OnBnClickedButtonStartPolling)
    ON_BN_CLICKED(IDC_BUTTON_STOP_POLLING, &CMy0430MFCAppDlg::OnBnClickedButtonStopPolling)
    ON_BN_CLICKED(IDC_BUTTON_CLEAR_LOG, &CMy0430MFCAppDlg::OnBnClickedButtonClearLog)
    ON_WM_CLOSE()
    ON_WM_TIMER()
    ON_MESSAGE(WM_SOCKET_CONNECT, &CMy0430MFCAppDlg::OnSocketConnect)
    ON_MESSAGE(WM_SOCKET_RECEIVE, &CMy0430MFCAppDlg::OnSocketReceive)
    ON_MESSAGE(WM_SOCKET_CLOSE, &CMy0430MFCAppDlg::OnSocketClose)
    ON_BN_CLICKED(IDC_BUTTON_CONNECT_PLC, &CMy0430MFCAppDlg::OnBnClickedButtonConnectPlc)

END_MESSAGE_MAP()

// CMy0430MFCAppDlg 메시지 처리기
BOOL CMy0430MFCAppDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // 이 대화 상자의 아이콘을 설정합니다.
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    // 소켓 초기화
    AfxSocketInit();

    // 리스트 컨트롤 초기화
    m_listIndicators.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    // 열 추가
    m_listIndicators.InsertColumn(0, _T("번호"), LVCFMT_LEFT, 40);
    m_listIndicators.InsertColumn(1, _T("IP 주소"), LVCFMT_LEFT, 100);
    m_listIndicators.InsertColumn(2, _T("포트"), LVCFMT_LEFT, 50);
    m_listIndicators.InsertColumn(3, _T("연결 상태"), LVCFMT_LEFT, 70);
    m_listIndicators.InsertColumn(4, _T("최대 표시"), LVCFMT_LEFT, 70);
    m_listIndicators.InsertColumn(5, _T("최소 눈금"), LVCFMT_LEFT, 70);
    m_listIndicators.InsertColumn(6, _T("소수점"), LVCFMT_LEFT, 60);
    m_listIndicators.InsertColumn(7, _T("AD 변환"), LVCFMT_LEFT, 70);
    m_listIndicators.InsertColumn(8, _T("측정 값"), LVCFMT_LEFT, 70);
    m_listIndicators.InsertColumn(9, _T("램프 상태"), LVCFMT_LEFT, 70);
    m_listIndicators.InsertColumn(10, _T("에러"), LVCFMT_LEFT, 50);

    // 일별 로그 파일 생성
    CreateDailyLogFile();

    // set.csv 파일 읽기
    if (!LoadIndicatorSettings(_T("set.csv"))) {
        AddLog(_T("인디케이터 설정 파일을 읽을 수 없습니다."));
    }

    // 리스트 컨트롤 업데이트
    UpdateListControl();

    // 버튼 초기화
    m_btnDisconnect.EnableWindow(FALSE);
    m_btnStartPolling.EnableWindow(FALSE);
    m_btnStopPolling.EnableWindow(FALSE);

    // 초기 로그
    AddLog(_T("인디케이터 통신 프로그램이 시작되었습니다."));

    // PLC 상태 초기화
    m_staticPLCStatus.SetWindowText(_T("PLC 상태: 연결 안됨"));
    m_nCurrentOperation = 0;  // 작업 번호 초기화

    // 프로그램 시작 시 자동으로 인디케이터 연결, PLC 연결, 폴링 시작
    PostMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON_CONNECT, BN_CLICKED), (LPARAM)GetDlgItem(IDC_BUTTON_CONNECT)->GetSafeHwnd());
    PostMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON_CONNECT_PLC, BN_CLICKED), (LPARAM)GetDlgItem(IDC_BUTTON_CONNECT_PLC)->GetSafeHwnd());

    // PLC와 인디케이터 연결 후 약간의 지연 시간을 두고 폴링 시작
    SetTimer(3, 2000, NULL); 

    return TRUE;  // 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
}

void CMy0430MFCAppDlg::OnPaint()
{
    if (IsIconic())
    {
        CPaintDC dc(this); // 그리기를 위한 디바이스 컨텍스트입니다.

        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

        // 클라이언트 사각형에서 아이콘을 가운데에 맞춥니다.
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;

        // 아이콘을 그립니다.
        dc.DrawIcon(x, y, m_hIcon);
    }
    else
    {
        CDialogEx::OnPaint();
    }
}

// 사용자가 최소화된 창을 끄는 동안에 커서가 표시되도록 시스템에서
// 이 함수를 호출합니다.
HCURSOR CMy0430MFCAppDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}

// 인디케이터 설정 파일 로드
BOOL CMy0430MFCAppDlg::LoadIndicatorSettings(const CString& filePath)
{
    // CString을 ANSI 문자열로 변환 (Unicode 빌드일 경우 필요)
    CT2CA pszFilePathA(filePath);
    std::string strFilePath(pszFilePathA);

    std::ifstream file(strFilePath);
    if (!file.is_open()) {
        return FALSE;
    }

    // m_indicators.clear(); 대신 카운트 초기화
    m_indicatorCount = 0;

    std::string line;
    // 헤더 라인 건너뛰기
    std::getline(file, line);

    // 인디케이터 정보 읽기
    while (std::getline(file, line) && m_indicatorCount < MAX_INDICATORS) {
        std::istringstream iss(line);
        std::string ip, port;

        if (std::getline(iss, ip, ',') && std::getline(iss, port, ',')) {
            IndicatorInfo& info = m_indicators[m_indicatorCount];
            info.ip = CString(ip.c_str());
            info.port = std::stoi(port);
            info.connected = false;
            info.maxCapacity = 0;
            info.minDivision = 0;
            info.decimalPoint = 0;
            info.adcValue = 0;
            info.measuredValue = 0;
            info.lampStatus = 0;
            info.errorData = 0;

            // 소켓 초기화
            info.socket.SetParent(this);
            info.socket.SetSocketID(m_indicatorCount); // 인덱스를 소켓 ID로 사용

            m_indicatorCount++;
        }
    }

    return m_indicatorCount > 0;
}

// 인디케이터 연결
BOOL CMy0430MFCAppDlg::ConnectToIndicator(int index)
{
    if (index < 0 || index >= (int)m_indicatorCount) {
        return FALSE;
    }

    IndicatorInfo& indicator = m_indicators[index];

    // 이미 연결된 상태면 리턴 (재연결 방지)
    if (indicator.connected && indicator.socket.IsConnected()) {
        return TRUE;
    }

    // 연결 해제 후 잠시 대기
    if (indicator.connected) {
        DisconnectIndicator(index);
        Sleep(100); // 소켓이 완전히 닫힐 때까지 잠시 대기
    }

    // 소켓 재생성
    indicator.socket.SetParent(this);
    indicator.socket.SetSocketID(index);

    // 이전 소켓이 남아있을 수 있으므로 강제 해제
    indicator.socket.Detach();

    // 소켓 생성
    if (!indicator.socket.Create()) {
        CString strError;
        strError.Format(_T("인디케이터 %s:%d 소켓 생성 실패: 오류 코드 %d"),
            indicator.ip, indicator.port, GetLastError());
        AddLog(strError);
        return FALSE;
    }

    // 연결 타임아웃 설정 (더 길게 설정)
    int timeout = 1000; // 1초로 증가
    indicator.socket.SetSockOpt(SO_SNDTIMEO, &timeout, sizeof(timeout));
    indicator.socket.SetSockOpt(SO_RCVTIMEO, &timeout, sizeof(timeout));

    // 연결
    if (!indicator.socket.Connect(indicator.ip, indicator.port)) {
        int nError = GetLastError();
        if (nError != WSAEWOULDBLOCK) {
            CString strError;
            strError.Format(_T("인디케이터 %s:%d 연결 실패: 오류 코드 %d"),
                indicator.ip, indicator.port, nError);
            AddLog(strError);
            indicator.socket.Close();
            return FALSE;
        }
    }

    CString strLog;
    strLog.Format(_T("인디케이터 %s:%d 연결 중..."), indicator.ip, indicator.port);
    AddLog(strLog);

    return TRUE;
}

// 인디케이터 연결 해제
void CMy0430MFCAppDlg::DisconnectIndicator(int index)
{
    if (index < 0 || index >= (int)m_indicatorCount) {
        return;
    }

    IndicatorInfo& indicator = m_indicators[index];

    if (indicator.connected) {
        indicator.socket.Close();
        indicator.connected = false;

        CString strLog;
        strLog.Format(_T("인디케이터 %s:%d 연결 해제"), indicator.ip, indicator.port);
        AddLog(strLog);
    }
}

// ModBus TCP를 통한 인디케이터 데이터 읽기
BOOL CMy0430MFCAppDlg::ReadAllIndicatorData()
{


    // 모든 인디케이터에 동시에 요청 보내기
    for (int i = 0; i < m_indicatorCount; i++) {
        if (m_indicators[i].connected && m_indicators[i].socket.IsConnected()) {
            // ModBus TCP 요청 패킷 구성
            BYTE requestPacket[12];
            memset(requestPacket, 0, sizeof(requestPacket));

            // MBAP 헤더 (7 바이트)
            WORD transactionId = g_transactionId++;
            requestPacket[0] = (transactionId >> 8) & 0xFF;  // Transaction ID High
            requestPacket[1] = transactionId & 0xFF;         // Transaction ID Low
            requestPacket[2] = 0x00;                         // Protocol ID High
            requestPacket[3] = 0x00;                         // Protocol ID Low
            requestPacket[4] = 0x00;                         // Length High
            requestPacket[5] = 0x06;                         // Length Low (6 바이트)
            requestPacket[6] = 0x01;                         // Unit ID

            // PDU (5 바이트)
            requestPacket[7] = MODBUS_FC_READ_HOLDING_REGISTERS;  // Function Code
            requestPacket[8] = 0x00;                              // Starting Address High
            requestPacket[9] = 0x00;                              // Starting Address Low
            requestPacket[10] = 0x00;                             // Number of Registers High
            requestPacket[11] = 0x0A;                             // Number of Registers Low (10개 레지스터)

            // 요청 전송 (바로 전송만 하고 응답은 OnSocketReceive에서 처리)
            m_indicators[i].socket.SendData(requestPacket, sizeof(requestPacket));
        }
    }

    return TRUE;
}

// 리스트 컨트롤 업데이트
void CMy0430MFCAppDlg::UpdateListControl()
{
    m_listIndicators.DeleteAllItems();

    for (size_t i = 0; i < m_indicatorCount; ++i) {
        const IndicatorInfo& indicator = m_indicators[i];

        CString strIndex;
        strIndex.Format(_T("%d"), i + 1);

        int nItem = m_listIndicators.InsertItem(i, strIndex);
        m_listIndicators.SetItemText(nItem, 1, indicator.ip);

        CString strPort;
        strPort.Format(_T("%d"), indicator.port);
        m_listIndicators.SetItemText(nItem, 2, strPort);

        m_listIndicators.SetItemText(nItem, 3, indicator.connected ? _T("연결됨") : _T("연결안됨"));

        if (indicator.connected) {
            CString strValue;

            strValue.Format(_T("%d"), indicator.maxCapacity);
            m_listIndicators.SetItemText(nItem, 4, strValue);

            strValue.Format(_T("%d"), indicator.minDivision);
            m_listIndicators.SetItemText(nItem, 5, strValue);

            strValue.Format(_T("%d"), indicator.decimalPoint);
            m_listIndicators.SetItemText(nItem, 6, strValue);

            strValue.Format(_T("%d"), indicator.adcValue);
            m_listIndicators.SetItemText(nItem, 7, strValue);

            strValue.Format(_T("%d"), indicator.measuredValue);
            m_listIndicators.SetItemText(nItem, 8, strValue);

            strValue.Format(_T("%d"), indicator.lampStatus);
            m_listIndicators.SetItemText(nItem, 9, strValue);

            strValue.Format(_T("%d"), indicator.errorData);
            m_listIndicators.SetItemText(nItem, 10, strValue);
        }
    }
}

// 타이머 핸들러
void CMy0430MFCAppDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (m_timerId == nIDEvent) {
        ExecuteSequentialOperation();
    }
    else if (m_plcHeartbeatTimerId == nIDEvent) {
        WritePLCHeartbeat();
    }
    else if (nIDEvent == 3) {
        // 자동 폴링 시작 타이머
        KillTimer(3);
        PostMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON_START_POLLING, BN_CLICKED),
            (LPARAM)GetDlgItem(IDC_BUTTON_START_POLLING)->GetSafeHwnd());
    }

    CDialogEx::OnTimer(nIDEvent);
}
// 연결 버튼 클릭
void CMy0430MFCAppDlg::OnBnClickedButtonConnect()
{
    UpdateData(TRUE);  // 컨트롤 -> 변수

    // 모든 인디케이터 연결
    bool allConnected = true;
    for (int i = 0; i < (int)m_indicatorCount; ++i) {
        if (!ConnectToIndicator(i)) {
            allConnected = false;
        }
    }

    // 버튼 상태 업데이트
    m_btnConnect.EnableWindow(!allConnected);
    m_btnDisconnect.EnableWindow(TRUE);
    m_btnStartPolling.EnableWindow(TRUE);
}

// 연결 해제 버튼 클릭
void CMy0430MFCAppDlg::OnBnClickedButtonDisconnect()
{
    // 모든 인디케이터 연결 해제
    for (int i = 0; i < (int)m_indicatorCount; ++i) {
        DisconnectIndicator(i);
    }

    // 타이머 중지
    if (m_bPolling) {
        KillTimer(m_timerId);
        m_timerId = 0;
        m_bPolling = false;
    }

    // 리스트 컨트롤 업데이트
    UpdateListControl();

    // 버튼 상태 업데이트
    m_btnConnect.EnableWindow(TRUE);
    m_btnDisconnect.EnableWindow(FALSE);
    m_btnStartPolling.EnableWindow(FALSE);
    m_btnStopPolling.EnableWindow(FALSE);
}

// 폴링 시작 버튼 클릭
void CMy0430MFCAppDlg::OnBnClickedButtonStartPolling()
{
    UpdateData(TRUE);

    // 기존 타이머 설정
    m_timerId = SetTimer(1, m_delayMs, NULL);
    m_bPolling = true;

    // PLC 하트비트 시작 추가
    StartPLCHeartbeat();

    CString strLog;
    strLog.Format(_T("데이터 폴링 시작: %d ms 간격"), m_delayMs);
    AddLog(strLog);

    m_btnStartPolling.EnableWindow(FALSE);
    m_btnStopPolling.EnableWindow(TRUE);
}

// 폴링 중지 버튼 클릭
void CMy0430MFCAppDlg::OnBnClickedButtonStopPolling()
{
    // 기존 타이머 중지
    KillTimer(m_timerId);
    m_timerId = 0;
    m_bPolling = false;

    // PLC 하트비트 중지 추가
    StopPLCHeartbeat();

    AddLog(_T("데이터 폴링 중지"));

    m_btnStartPolling.EnableWindow(TRUE);
    m_btnStopPolling.EnableWindow(FALSE);
}

// 로그 지우기 버튼 클릭
void CMy0430MFCAppDlg::OnBnClickedButtonClearLog()
{
    m_logList.ResetContent();
    AddLog(_T("로그를 지웠습니다."));
}

// 대화 상자 닫기
void CMy0430MFCAppDlg::OnClose()
{
    // 타이머 중지
    if (m_timerId != 0) {
        KillTimer(m_timerId);
        m_timerId = 0;
    }

    // 모든 인디케이터 연결 해제
    for (int i = 0; i < (int)m_indicatorCount; ++i) {
        DisconnectIndicator(i);
    }

    CDialogEx::OnClose();
}

// 소켓 연결 메시지 처리
LRESULT CMy0430MFCAppDlg::OnSocketConnect(WPARAM wParam, LPARAM lParam)
{
    int nSocketID = (int)wParam;
    int nErrorCode = (int)lParam;

    // PLC 소켓 연결 처리 (ID가 999인 경우)
    if (nSocketID == 999) {
        if (nErrorCode == 0) {
            // PLC 연결 성공
            m_staticPLCStatus.SetWindowText(_T("PLC 상태: 연결됨"));
            AddLog(_T("PLC 연결 성공"));
        }
        else {
            // PLC 연결 실패
            m_staticPLCStatus.SetWindowText(_T("PLC 상태: 연결 실패"));
            AddLog(_T("PLC 연결 실패"));
        }
        return 0;
    }

    if (nSocketID >= 0 && nSocketID < (int)m_indicatorCount) {
        IndicatorInfo& indicator = m_indicators[nSocketID];

        if (nErrorCode == 0) {
            indicator.connected = true;

            CString strLog;
            strLog.Format(_T("인디케이터 %s:%d 연결 성공"), indicator.ip, indicator.port);
            AddLog(strLog);
            // 연결 성공 시 정상 상태 보고
            ReportIndicatorErrorToPLC(nSocketID, false);
        }
        else {
            indicator.connected = false;

            CString strError;
            strError.Format(_T("인디케이터 %s:%d 연결 실패: 오류 코드 %d"),
                indicator.ip, indicator.port, nErrorCode);
            AddLog(strError);
            // 연결 실패 시 오류 상태 보고
            ReportIndicatorErrorToPLC(nSocketID, true);
        }

        // UI 업데이트
        UpdateListControl();
    }

    return 0;
}

// 소켓 데이터 수신 메시지 처리
LRESULT CMy0430MFCAppDlg::OnSocketReceive(WPARAM wParam, LPARAM lParam)
{
    int nSocketID = (int)wParam;
    int nBytesAvailable = (int)lParam;

    // PLC 소켓으로부터 받은 데이터 처리 (ID가 999인 경우)
    if (nSocketID == 999) {
        std::vector<BYTE>& buffer = m_plcSocket.m_RecvBuffer;

        if (!buffer.empty()) {
            // 디버깅을 위한 패킷 로깅
            CString strPacketData;
            strPacketData = _T("PLC 수신 데이터: ");
            for (int i = 0; i < min(32, (int)buffer.size()); i++) {
                CString temp;
                temp.Format(_T("%02X "), buffer[i]);
                strPacketData += temp;
            }
            if (buffer.size() > 32) strPacketData += _T("...");
            AddLog(strPacketData);

            // XGT 전용 프로토콜 응답 검사 (Company ID "LSIS-XGT" 확인)
            if (buffer.size() >= 20 &&
                buffer[0] == 0x4C && buffer[1] == 0x53 && buffer[2] == 0x49 && buffer[3] == 0x53 &&
                buffer[4] == 0x2D && buffer[5] == 0x58 && buffer[6] == 0x47 && buffer[7] == 0x54) {

                // XGT 프로토콜 응답 로깅
                CString strLog;
                strLog.Format(_T("XGT 프로토콜 응답 수신: 길이=%d 바이트"), buffer.size());
                AddLog(strLog);

                // Source of Frame 확인 (PLC -> Client: 0x11)
                if (buffer[13] == 0x11) {
                    // Invoke ID 추출 (요청과 응답 매칭)
                    WORD invokeId = (buffer[15] << 8) | buffer[14];

                    // 데이터 길이 추출
                    WORD dataLength = (buffer[17] << 8) | buffer[16];

                    // 명령어 확인 (20,21 바이트)
                    WORD command = (buffer[21] << 8) | buffer[20];

                    strLog.Format(_T("XGT 응답: InvokeID=0x%04X, Length=%d, Command=0x%04X"),
                        invokeId, dataLength, command);
                    AddLog(strLog);

                    // 읽기 응답 (0x0055)
                    if (command == 0x0055 && buffer.size() >= 28) {
                        // 데이터 타입 확인
                        WORD dataType = (buffer[23] << 8) | buffer[22];
                        strLog.Format(_T("XGT 읽기 응답: 데이터 타입=0x%04X"), dataType);
                        AddLog(strLog);

                        // 예약 영역 건너뛰기 (2바이트)

                        // 에러 상태 확인 (26,27 바이트)
                        WORD errorState = (buffer[27] << 8) | buffer[26];

                        if (errorState == 0) {
                            // 변수 개수 및 데이터 블록 확인
                            WORD blockCount = (buffer[29] << 8) | buffer[28];

                            if (blockCount > 0 && buffer.size() >= 32) {
                                // 데이터 크기 (30,31 바이트)
                                WORD dataSize = (buffer[31] << 8) | buffer[30];

                                if (buffer.size() >= 32 + dataSize) {
                                    // 데이터 읽기 (32번째 바이트부터)
                                    WORD commandValue = 0;
                                    if (dataSize == 2) {  // Word 타입
                                        commandValue = (buffer[33] << 8) | buffer[32];
                                    }

                                    // 어떤 인디케이터에 대한 명령인지 확인 (InvokeID로 매핑)
                                    // 여기서는 간단히 InvokeID에서 추출 (실제로는 요청 시 저장한 매핑 필요)
                                    int indicatorIndex = invokeId % 100;  // 간단한 예시

                                    // 명령값이 유효한지 확인
                                    if (indicatorIndex >= 0 && indicatorIndex < m_indicatorCount) {
                                        strLog.Format(_T("PLC로부터 인디케이터 %d의 외부입력명령 0x%04X를 읽었습니다."),
                                            indicatorIndex + 1, commandValue);
                                        AddLog(strLog);

                                        // 명령이 0이 아닌 경우 인디케이터로 전송
                                        if (commandValue != 0 && m_indicators[indicatorIndex].connected) {
                                            SendCommandToIndicator(indicatorIndex, commandValue);


                                        }
                                    }
                                }
                            }
                        }
                        else {
                            CString strError;
                            strError.Format(_T("PLC 데이터 읽기 에러: 0x%04X"), errorState);
                            AddLog(strError);
                        }
                    }
                    // 쓰기 응답 (0x0059)
                    else if (command == 0x0059 && buffer.size() >= 28) {
                        // 데이터 타입 확인
                        WORD dataType = (buffer[23] << 8) | buffer[22];
                        strLog.Format(_T("XGT 쓰기 응답: 데이터 타입=0x%04X"), dataType);
                        AddLog(strLog);

                        // 예약 영역 건너뛰기 (2바이트)

                        // 에러 상태 확인 (26,27 바이트)
                        WORD errorState = (buffer[27] << 8) | buffer[26];

                        if (errorState == 0) {
                            // 성공적으로 쓰기 완료
                            WORD blockCount = (buffer[29] << 8) | buffer[28];

                            // 어떤 인디케이터에 대한 쓰기인지 확인
                            int indicatorIndex = invokeId % 100;  // 간단한 예시

                            if (indicatorIndex >= 0 && indicatorIndex < m_indicatorCount) {
                                strLog.Format(_T("인디케이터 %d 측정값을 PLC에 성공적으로 썼습니다."),
                                    indicatorIndex + 1);
                                AddLog(strLog);
                            }
                            else {
                                strLog.Format(_T("PLC 데이터 쓰기 성공: Invoke ID=0x%04X"), invokeId);
                                AddLog(strLog);
                            }
                        }
                        else {
                            CString strError;
                            strError.Format(_T("PLC 데이터 쓰기 에러: 0x%04X, Invoke ID=0x%04X"),
                                errorState, invokeId);
                            AddLog(strError);
                        }
                    }
                }
            }
            // ModbusTCP 응답 처리
            else if (buffer.size() >= 8) {
                // 트랜잭션 ID 추출
                WORD transactionId = (buffer[0] << 8) | buffer[1];

                // 모드버스 함수 코드 확인
                BYTE functionCode = buffer[7];

                // ModBus-TCP 패킷 분석 및 로그
                CString strAnalysis = AnalyzeModbusPacket(buffer.data(), (int)buffer.size());
                if (!strAnalysis.IsEmpty()) {
                    CString strFullLog;
                    strFullLog.Format(_T("[PLC %s:%d] %s"),
                        m_strPLCIP, m_nPLCPort, strAnalysis);
                    AddLog(strFullLog);
                }

                // Read Holding Registers 응답 처리 (외부입력명령 읽기 응답)
                if (functionCode == 0x03 && buffer.size() >= 9) {
                    BYTE dataLength = buffer[8];
                    if (buffer.size() >= 9 + dataLength && dataLength >= 2) {  // 1 레지스터 * 2 바이트
                        // 트랜잭션 ID로 어떤 인디케이터에 대한 명령인지 구분

                        // 데이터 파싱 (레지스터 값)
                        WORD commandValue = (buffer[9] << 8) | buffer[10];

                        // 트랜잭션 ID와 인디케이터 인덱스 매핑
                        int indicatorIndex = -1;
                        for (int i = 0; i < m_indicatorCount; i++) {
                            // PLC 주소로 인디케이터 인덱스 확인
                            WORD regAddr = 6005 + (i * 10);
                            WORD readAddr = 0;

                            // 주소 추출 로직 (예시)
                            if (buffer.size() >= 12) {
                                readAddr = (buffer[8 + 2] << 8) | buffer[9 + 2];
                            }

                            if (regAddr == readAddr) {
                                indicatorIndex = i;
                                break;
                            }
                        }

                        if (indicatorIndex >= 0 && indicatorIndex < m_indicatorCount) {
                            CString strLog;
                            strLog.Format(_T("PLC로부터 인디케이터 %d의 외부입력명령 0x%04X를 받았습니다."),
                                indicatorIndex + 1, commandValue);
                            AddLog(strLog);

                            // 명령이 0이 아닌 경우 인디케이터로 전송
                            if (commandValue != 0) {
                                SendCommandToIndicator(indicatorIndex, commandValue);


                            }
                        }
                    }
                }
                // Write Multiple Registers 또는 Write Single Register 응답 처리
                else if ((functionCode == 0x10 || functionCode == 0x06) && buffer.size() >= 12) {
                    // 쓰기 응답은 단순 확인용이므로 로그만 추가
                    CString strLog;
                    strLog.Format(_T("ModbusTCP PLC 데이터 쓰기 확인: 트랜잭션=%04X, 함수=%02X"),
                        transactionId, functionCode);
                    AddLog(strLog);
                }
            }

            // 버퍼 비우기
            buffer.clear();
        }
        return 0;
    }

    // 인디케이터 소켓 처리
    if (nSocketID >= 0 && nSocketID < m_indicatorCount) {
        IndicatorInfo& indicator = m_indicators[nSocketID];
        std::vector<BYTE>& buffer = indicator.socket.m_RecvBuffer;

        if (!buffer.empty()) {
            // ModBus-TCP 패킷 분석 및 로그
            CString strAnalysis = AnalyzeModbusPacket(buffer.data(), (int)buffer.size());
            if (!strAnalysis.IsEmpty()) {
                CString strFullLog;
                strFullLog.Format(_T("[인디케이터 %s:%d] %s"),
                    indicator.ip, indicator.port, strAnalysis);
                AddLog(strFullLog);
            }

            // 응답 패킷 분석 (최소 MBAP 헤더 + 함수 코드 + 바이트 수 확인)
            if (buffer.size() >= 9 && buffer[7] == MODBUS_FC_READ_HOLDING_REGISTERS) {
                // 데이터 길이 확인
                BYTE dataLength = buffer[8];
                if (buffer.size() >= 9 + dataLength && dataLength >= 20) {  // 10 레지스터 * 2 바이트
                    // 데이터 파싱
                    int offset = 9;  // 데이터 시작 위치

                    // 최대 표시 (2 워드)
                    indicator.maxCapacity = (buffer[offset] << 24) | (buffer[offset + 1] << 16) |
                        (buffer[offset + 2] << 8) | buffer[offset + 3];
                    offset += 4;

                    // 최소 눈금 (1 워드)
                    indicator.minDivision = (buffer[offset] << 8) | buffer[offset + 1];
                    offset += 2;

                    // 소수점 위치 (1 워드)
                    indicator.decimalPoint = (buffer[offset] << 8) | buffer[offset + 1];
                    offset += 2;

                    // AD 변환 값 (2 워드)
                    indicator.adcValue = (buffer[offset] << 24) | (buffer[offset + 1] << 16) |
                        (buffer[offset + 2] << 8) | buffer[offset + 3];
                    offset += 4;

                    // 측정 값 (2 워드)
                    indicator.measuredValue = (buffer[offset] << 24) | (buffer[offset + 1] << 16) |
                        (buffer[offset + 2] << 8) | buffer[offset + 3];
                    offset += 4;

                    // 램프 상태 (1 워드)
                    indicator.lampStatus = (buffer[offset] << 8) | buffer[offset + 1];
                    offset += 2;

                    // 에러 데이터 (1 워드)
                    indicator.errorData = (buffer[offset] << 8) | buffer[offset + 1];

                    // UI 업데이트
                    UpdateListControl();
                    // 데이터 읽기 성공 시 정상 상태 보고
                    ReportIndicatorErrorToPLC(nSocketID, false);

                    // 인디케이터 데이터를 읽은 후 PLC에 쓰기 (XGT 프로토콜로 변환)
                    if (m_plcSocket.IsConnected()) {
                        WriteIndicatorValueToPLC(nSocketID);
                    }
                }
            }
            // Write Single Register 응답 처리 (외부입력명령 전송 결과)
            else if (buffer.size() >= 12 && buffer[7] == 0x06) {
                WORD regAddr = (buffer[8] << 8) | buffer[9];
                WORD regValue = (buffer[10] << 8) | buffer[11];

                if (regAddr == 0x40) {  // 외부입력명령 레지스터
                    CString strLog;
                    strLog.Format(_T("인디케이터 %d 외부입력명령 0x%04X 설정 완료"),
                        nSocketID + 1, regValue);
                    AddLog(strLog);
                }
            }

            // 버퍼 비우기
            buffer.clear();
        }
    }

    return 0;
}


// 소켓 연결 종료 메시지 처리
LRESULT CMy0430MFCAppDlg::OnSocketClose(WPARAM wParam, LPARAM lParam)
{
    int nSocketID = (int)wParam;
    int nErrorCode = (int)lParam;

    // 에러 코드에 따른 원인 문자열
    CString strErrorReason;

    // 에러 코드 분석
    switch (nErrorCode)
    {
    case 0:
        strErrorReason = _T("정상 종료");
        break;
    case WSAECONNABORTED:
        strErrorReason = _T("연결 중단됨 (소프트웨어로 인한 연결 중단)");
        break;
    case WSAECONNRESET:
        strErrorReason = _T("연결 리셋됨 (원격 호스트에 의해 강제 종료)");
        break;
    case WSAEHOSTUNREACH:
        strErrorReason = _T("목적지 호스트에 도달할 수 없음");
        break;
    case WSAENETDOWN:
        strErrorReason = _T("네트워크 다운됨");
        break;
    case WSAENETRESET:
        strErrorReason = _T("네트워크 연결이 리셋됨");
        break;
    case WSAETIMEDOUT:
        strErrorReason = _T("연결 시간 초과");
        break;
    default:
        strErrorReason.Format(_T("알 수 없는 오류 코드: %d"), nErrorCode);
        break;
    }

    // PLC 소켓 종료 처리
    if (nSocketID == 999) {
        CString strLog;
        strLog.Format(_T("PLC 연결이 종료되었습니다. 원인: %s"), strErrorReason);
        AddLog(strLog);


        // UI 업데이트
        m_staticPLCStatus.SetWindowText(_T("PLC 상태: 연결 안됨"));

        // 연결이 비정상적으로 끊긴 경우 WSAGetLastError로 추가 정보 확인
        if (nErrorCode != 0) {
            int nWSAError = WSAGetLastError();
            if (nWSAError != 0) {
                CString strWSAError;
                strWSAError.Format(_T("WSA 에러 코드: %d"), nWSAError);
                AddLog(strWSAError);
            }
        }

        return 0;
    }

    // 인디케이터 소켓 종료 처리
    if (nSocketID >= 0 && nSocketID < (int)m_indicatorCount) {
        IndicatorInfo& indicator = m_indicators[nSocketID];

        if (indicator.connected) {
            indicator.connected = false;

            CString strLog;
            strLog.Format(_T("인디케이터 %s:%d 연결 종료됨. 원인: %s"),
                indicator.ip, indicator.port, strErrorReason);
            AddLog(strLog);

            // 네트워크 관련 추가 오류 정보 확인
            int nWSAError = WSAGetLastError();
            if (nWSAError != 0) {
                CString strWSAError;
                strWSAError.Format(_T("인디케이터 %d WSA 에러 코드: %d"), nSocketID + 1, nWSAError);
                AddLog(strWSAError);
            }
            // 인디케이터 연결 종료 시 오류 상태 보고
            ReportIndicatorErrorToPLC(nSocketID, true);
            // UI 업데이트
            UpdateListControl();
        }
    }

    return 0;
}

// 로그 추가 (텍스트만)
void CMy0430MFCAppDlg::AddLog(LPCTSTR pszLog)
{
    CTime time = CTime::GetCurrentTime();
    CString strTimedLog;
    strTimedLog.Format(_T("[%02d:%02d:%02d] %s"),
        time.GetHour(), time.GetMinute(), time.GetSecond(), pszLog);

    // 리스트 박스에 추가
    int nIndex = m_logList.AddString(strTimedLog);
    m_logList.SetTopIndex(nIndex);

    // 로그 항목 제한
    LimitLogItems();

    // 연결 관련 로그인지 확인하여 파일에 저장
    CString strLogLower = pszLog;
    strLogLower.MakeLower();

    // 연결 관련 키워드 확인
    if (strLogLower.Find(_T("연결")) != -1 ||
        strLogLower.Find(_T("connect")) != -1 ||
        strLogLower.Find(_T("disconnect")) != -1 ||
        strLogLower.Find(_T("오류")) != -1 ||
        strLogLower.Find(_T("error")) != -1 ||
        strLogLower.Find(_T("exception")) != -1 ||
        strLogLower.Find(_T("예외")) != -1 ||
        strLogLower.Find(_T("시작")) != -1 ||
        strLogLower.Find(_T("종료")) != -1) {

        // 파일에 저장
        SaveLogToFile(pszLog);
    }
}


// 로그 추가 (데이터 포함)
void CMy0430MFCAppDlg::AddLog(LPCTSTR pszPrefix, const BYTE* pData, int nLength)
{
    CString strLog = pszPrefix;
    CString strTemp;

    // 16진수 형식으로 데이터 추가 (최대 32바이트만 표시)
    int nMaxDisplay = min(nLength, 32);
    for (int i = 0; i < nMaxDisplay; i++) {
        strTemp.Format(_T("%02X "), pData[i]);
        strLog += strTemp;
    }

    if (nLength > nMaxDisplay)
        strLog += _T("... ");

    strLog.Format(_T("%s [%d바이트]"), strLog, nLength);

    // 로그 리스트에 추가
    AddLog(strLog);
}

// ModBus-TCP 패킷 분석
CString CMy0430MFCAppDlg::AnalyzeModbusPacket(const BYTE* pData, int nLength)
{
    // 최소 Modbus-TCP 헤더 길이 확인
    if (nLength < 7)
        return _T("");

    CString strResult;

    // MBAP 헤더 분석
    WORD transactionID = (pData[0] << 8) | pData[1];
    WORD protocolID = (pData[2] << 8) | pData[3];
    WORD length = (pData[4] << 8) | pData[5];
    BYTE unitID = pData[6];

    strResult.Format(_T("Modbus-TCP: TrID=%04X, Length=%d, UnitID=%d"),
        transactionID, length, unitID);

    // 기능 코드 분석
    if (nLength >= 8) {
        BYTE functionCode = pData[7];
        CString strFunction;

        switch (functionCode) {
        case 0x03:
            strFunction = _T("Read Holding Registers");
            break;
        case 0x06:
            strFunction = _T("Write Single Register");
            break;
        case 0x10:
            strFunction = _T("Write Multiple Registers");
            break;
        default:
            strFunction.Format(_T("Function 0x%02X"), functionCode);
            break;
        }

        strResult.AppendFormat(_T(", Function=%s"), strFunction);

        // 기능별 추가 분석
        if (functionCode == 0x03 && nLength >= 12) {  // Read Holding Registers (요청)
            WORD startAddr = (pData[8] << 8) | pData[9];
            WORD regCount = (pData[10] << 8) | pData[11];
            strResult.AppendFormat(_T(", StartAddr=0x%04X, Count=%d"), startAddr, regCount);
        }
        else if (functionCode == 0x03 && nLength >= 9) {  // Read Holding Registers (응답)
            BYTE byteCount = pData[8];
            strResult.AppendFormat(_T(", ByteCount=%d"), byteCount);
        }
        else if (functionCode == 0x06 && nLength >= 12) {  // Write Single Register
            WORD regAddr = (pData[8] << 8) | pData[9];
            WORD regValue = (pData[10] << 8) | pData[11];
            strResult.AppendFormat(_T(", RegAddr=0x%04X, Value=0x%04X"), regAddr, regValue);
        }
        else if (functionCode == 0x10 && nLength >= 13) {  // Write Multiple Registers (요청)
            WORD startAddr = (pData[8] << 8) | pData[9];
            WORD regCount = (pData[10] << 8) | pData[11];
            BYTE byteCount = pData[12];
            strResult.AppendFormat(_T(", StartAddr=0x%04X, Count=%d, Bytes=%d"),
                startAddr, regCount, byteCount);
        }
    }

    return strResult;
}

// PLC 연결 함수
BOOL CMy0430MFCAppDlg::ConnectToPLC(const CString& ipAddress, int port)
{
    // 이미 연결되어 있다면 연결 해제
    if (m_plcSocket.IsConnected()) {
        DisconnectPLC();
    }

    // XGT 전용 프로토콜은 포트 2004 사용
    // 기본 포트가 아니라면 로그에 경고 표시
    if (port != 2004) {
        CString strWarning;
        strWarning.Format(_T("주의: XGT 전용 프로토콜은 일반적으로 포트 2004를 사용합니다. 현재 설정: %d"), port);
        AddLog(strWarning);
    }

    // 소켓 초기화 전에 부모와 ID 설정
    m_plcSocket.SetParent(this);
    m_plcSocket.SetSocketID(999);  // PLC 소켓용 특별 ID

    // 소켓 생성
    if (!m_plcSocket.Create()) {
        CString strError;
        strError.Format(_T("PLC %s:%d 소켓 생성 실패: 오류 코드 %d"),
            ipAddress, port, GetLastError());
        AddLog(strError);
        return FALSE;
    }

    // 연결 타임아웃 설정
    int timeout = 1000;
    m_plcSocket.SetSockOpt(SO_SNDTIMEO, &timeout, sizeof(timeout));
    m_plcSocket.SetSockOpt(SO_RCVTIMEO, &timeout, sizeof(timeout));
    int rcvBufSize = 8192; // 수신 버퍼 크기 증가
    int sndBufSize = 8192; // 송신 버퍼 크기 증가
    m_plcSocket.SetSockOpt(SO_RCVBUF, &rcvBufSize, sizeof(rcvBufSize));
    m_plcSocket.SetSockOpt(SO_SNDBUF, &sndBufSize, sizeof(sndBufSize));

    // PLC 연결 시도
    if (!m_plcSocket.Connect(ipAddress, port)) {
        int nError = GetLastError();
        if (nError != WSAEWOULDBLOCK) {
            CString strError;
            strError.Format(_T("PLC %s:%d 연결 실패: 오류 코드 %d"),
                ipAddress, port, nError);
            AddLog(strError);
            m_plcSocket.Close();
            return FALSE;
        }
    }

    CString strLog;
    strLog.Format(_T("PLC %s:%d 연결을 시도합니다... (XGT 전용 프로토콜)"), ipAddress, port);
    AddLog(strLog);
    return TRUE;
}
void CMy0430MFCAppDlg::DisconnectPLC()
{
    if (m_plcSocket.IsConnected()) {
        m_plcSocket.Close();
        m_staticPLCStatus.SetWindowText(_T("PLC 상태: 연결 안됨"));
        AddLog(_T("PLC 연결이 해제되었습니다."));
    }
}
void CMy0430MFCAppDlg::OnBnClickedButtonConnectPlc()
{
    UpdateData(TRUE); // 컨트롤 데이터를 변수로 가져오기

    // 입력값 검증
    if (m_strPLCIP.IsEmpty())
    {
        AfxMessageBox(_T("PLC IP 주소를 입력하세요."));
        m_editPLCIP.SetFocus();
        return;
    }

    if (m_nPLCPort <= 0 || m_nPLCPort > 65535)
    {
        AfxMessageBox(_T("올바른 포트 번호를 입력하세요(1-65535)."));
        m_editPLCPort.SetFocus();
        return;
    }

    try {
        // PLC 연결 시도
        if (ConnectToPLC(m_strPLCIP, m_nPLCPort))
        {
            CString strLog;
            strLog.Format(_T("PLC %s:%d 연결을 시도합니다..."), m_strPLCIP, m_nPLCPort);
            AddLog(strLog);
        }
        else
        {
            AddLog(_T("PLC 연결 시도 실패"));
        }
    }
    catch (CException* e) {
        TCHAR szError[1024];
        e->GetErrorMessage(szError, 1024);
        AddLog(szError);
        e->Delete();
    }
}

// PLC에 인디케이터 측정값을 쓰는 함수 - XGT 프로토콜 사용
BOOL CMy0430MFCAppDlg::WriteIndicatorValueToPLC(int indicatorIndex)
{
    if (indicatorIndex < 0 || indicatorIndex >= m_indicatorCount) {
        return FALSE;
    }

    // PLC가 연결되어 있지 않으면 실패
    if (!m_plcSocket.IsConnected()) {
        AddLog(_T("PLC에 연결되어 있지 않아 데이터를 쓸 수 없습니다."));
        return FALSE;
    }

    try {
        IndicatorInfo& indicator = m_indicators[indicatorIndex];

        // PLC 메모리 주소 계산 (각 인디케이터는 10 간격으로 D6001부터 시작)
        WORD plcAddress = 6001 + (indicatorIndex * 10);

        // DW로 변경 -  Word 타입 사용

        // XGT 전용 프로토콜 패킷 구성 - 연속 쓰기(바이트 단위)

        // 1. Company ID + 헤더 (20 바이트)
        BYTE header[20] = {
            0x4C, 0x53, 0x49, 0x53, 0x2D, 0x58, 0x47, 0x54, 0x00, 0x00, // "LSIS-XGT"
            0x00, 0x00,  // PLC Info
            0xA0,        // CPU Info
            0x33,        // Source of Frame (0x33: Client->Server)
            0x00, 0x01,  // Invoke ID (임의 값)
            0x00, 0x00,  // Length (나중에 설정)
            0x00,        // FEnet Position
            0x00         // Reserved
        };

        // 2. 명령어 (Write Request: 0x0058)
        BYTE command[2] = { 0x58, 0x00 };

        // 3. 데이터 타입 (Double Word: 0x0003에서 Word: 0x0002로 변경)
        BYTE dataType[2] = { 0x02, 0x00 };

        // 4. 예약 영역
        BYTE reserved[2] = { 0x00, 0x00 };

        // 5. 변수 개수 (1개)
        BYTE varCount[2] = { 0x01, 0x00 };

        // 6. 변수 이름 길이 
        CString strVarName;
        strVarName.Format(_T("%%DW%d"), plcAddress); // DW로 변경
        CT2CA pszVarName(strVarName);
        int varNameLen = strlen(pszVarName);
        BYTE varNameLength[2] = { (BYTE)varNameLen, 0x00 };

        // 7. 변수 이름 
        std::vector<BYTE> varName(varNameLen);
        memcpy(varName.data(), pszVarName, varNameLen);

        // 8. 데이터 크기 (2 바이트로 변경 - WORD)
        BYTE dataSize[2] = { 0x02, 0x00 };

        // 9. 데이터 (측정값 - 16비트로 변경)
        // XGT 프로토콜은 리틀 엔디안(최하위 바이트 먼저)을 사용
        BYTE data[2];
        data[0] = (indicator.measuredValue >> 0) & 0xFF;   // 최하위 바이트
        data[1] = (indicator.measuredValue >> 8) & 0xFF;   // 상위 바이트
        // 주의: 상위 2바이트 데이터는 손실됩니다 (16비트 초과 값 사용 시 주의)

        // 총 패킷 길이 계산 (헤더 제외)
        int dataLength = sizeof(command) + sizeof(dataType) + sizeof(reserved) +
            sizeof(varCount) + sizeof(varNameLength) + varNameLen +
            sizeof(dataSize) + sizeof(data);

        // 헤더의 Length 필드 업데이트
        header[16] = dataLength & 0xFF;
        header[17] = (dataLength >> 8) & 0xFF;

        // 전체 패킷 구성
        std::vector<BYTE> packet;
        packet.insert(packet.end(), header, header + sizeof(header));
        packet.insert(packet.end(), command, command + sizeof(command));
        packet.insert(packet.end(), dataType, dataType + sizeof(dataType));
        packet.insert(packet.end(), reserved, reserved + sizeof(reserved));
        packet.insert(packet.end(), varCount, varCount + sizeof(varCount));
        packet.insert(packet.end(), varNameLength, varNameLength + sizeof(varNameLength));
        packet.insert(packet.end(), varName.begin(), varName.end());
        packet.insert(packet.end(), dataSize, dataSize + sizeof(dataSize));
        packet.insert(packet.end(), data, data + sizeof(data));

        // 요청 전송
        if (!m_plcSocket.SendData(packet.data(), packet.size())) {
            CString strError;
            strError.Format(_T("PLC에 인디케이터 %d 측정값 쓰기 실패"), indicatorIndex + 1);
            AddLog(strError);
            return FALSE;
        }

        CString strLog;
        strLog.Format(_T("인디케이터 %d 측정값(%d)을 PLC 주소 D%d에 쓰기 요청 (XGT 프로토콜, DW 타입)"),
            indicatorIndex + 1, indicator.measuredValue, plcAddress);
        AddLog(strLog);

        return TRUE;
    }
    catch (CException* e) {
        TCHAR szError[1024];
        e->GetErrorMessage(szError, 1024);
        CString strError;
        strError.Format(_T("PLC 데이터 쓰기 중 예외 발생: %s"), szError);
        AddLog(strError);
        e->Delete();
        return FALSE;
    }
}
// PLC에서 외부입력명령을 읽어 인디케이터에 보내는 함수 - XGT 프로토콜 사용
BOOL CMy0430MFCAppDlg::ReadCommandFromPLCToIndicator(int indicatorIndex)
{
    if (indicatorIndex < 0 || indicatorIndex >= m_indicatorCount || !m_indicators[indicatorIndex].connected) {
        return FALSE;
    }

    // PLC가 연결되어 있지 않으면 실패
    if (!m_plcSocket.IsConnected()) {
        return FALSE;
    }

    try {
        
        WORD plcAddress = 6005;  // 고정된 주소

        // XGT 전용 프로토콜 패킷 구성 - 읽기 요청
        const int BUFFER_SIZE = 512;
        BYTE sendBuffer[BUFFER_SIZE] = { 0, };

        // Company ID (LSIS-XGT)
        sendBuffer[0] = 0x4C;  // 'L'
        sendBuffer[1] = 0x53;  // 'S'
        sendBuffer[2] = 0x49;  // 'I'
        sendBuffer[3] = 0x53;  // 'S'
        sendBuffer[4] = 0x2D;  // '-'
        sendBuffer[5] = 0x58;  // 'X'
        sendBuffer[6] = 0x47;  // 'G'
        sendBuffer[7] = 0x54;  // 'T'
        sendBuffer[8] = 0x00;  // '\0'
        sendBuffer[9] = 0x00;  // '\0'

        // PLC Info (Don't care)
        sendBuffer[10] = 0x00;
        sendBuffer[11] = 0x00;

        // CPU Info
        sendBuffer[12] = 0xA0;

        // Source of Frame (PC -> PLC)
        sendBuffer[13] = 0x33;

        // Invoke ID 
        sendBuffer[14] = 0x02;
        sendBuffer[15] = 0x00;

        // 메모리 주소 문자열 생성
        CString strVarName;
        strVarName.Format(_T("%%DW%d"), plcAddress);
        CStringA strMemAddressA(strVarName);
        int memAddrLen = strMemAddressA.GetLength();

        // 계산된 데이터 길이
        int dataLength = 10 + memAddrLen;  // 명령어(2) + 데이터타입(2) + 예약영역(2) + 블록수(2) + 변수길이(2) + 변수(memAddrLen)

        // Length
        sendBuffer[16] = (BYTE)(dataLength & 0xFF);
        sendBuffer[17] = (BYTE)((dataLength >> 8) & 0xFF);

        // FEnet Position
        sendBuffer[18] = 0x00;

        // Reserved
        sendBuffer[19] = 0x00;

        // Command (읽기 요청: 0x0054)
        sendBuffer[20] = 0x54;
        sendBuffer[21] = 0x00;

        // Data Type (워드: 0x0002)
        sendBuffer[22] = 0x02;
        sendBuffer[23] = 0x00;

        // 예약 영역
        sendBuffer[24] = 0x00;
        sendBuffer[25] = 0x00;

        // 블록 수 (1개의 블록)
        sendBuffer[26] = 0x01;
        sendBuffer[27] = 0x00;

        // 변수 길이 (메모리 주소 문자열 길이)
        sendBuffer[28] = (BYTE)(memAddrLen & 0xFF);
        sendBuffer[29] = (BYTE)((memAddrLen >> 8) & 0xFF);

        // 변수 (메모리 주소 문자열)
        for (int i = 0; i < memAddrLen; i++) {
            sendBuffer[30 + i] = strMemAddressA[i];
        }

        // 데이터 전송
        int totalSize = dataLength + 20;  // 헤더(20) + 데이터길이

        CString strLog;
        strLog.Format(_T("PLC 주소 D%d에서 인디케이터 %d 명령 읽기 요청 전송 (총 %d 바이트)"),
            plcAddress, indicatorIndex + 1, totalSize);
        AddLog(strLog);

        // 디버깅 용 패킷 로그
        CString strPacket = _T("전송 패킷: ");
        for (int i = 0; i < min(totalSize, 50); i++) {
            CString temp;
            temp.Format(_T("%02X "), sendBuffer[i]);
            strPacket += temp;
        }
        if (totalSize > 50) strPacket += _T("...");
        AddLog(strPacket);

        // Send 함수는 socket.SendData 대신 직접 Send 사용
        int sendSize = m_plcSocket.Send(sendBuffer, totalSize);

        if (sendSize != totalSize) {
            CString strError;
            strError.Format(_T("PLC 명령 읽기 요청 전송 실패: %d/%d 바이트"), sendSize, totalSize);
            AddLog(strError);
            return FALSE;
        }

        // 응답 대기 (동기식)
        BYTE recvBuffer[BUFFER_SIZE] = { 0, };

        // 타임아웃 설정 (1초)
        int timeout = 1000;
        DWORD startTime = GetTickCount();

        int recvSize = 0;
        while ((GetTickCount() - startTime) < (DWORD)timeout) {
            // 소켓에서 데이터 수신 시도
            recvSize = m_plcSocket.Receive(recvBuffer, BUFFER_SIZE);
            if (recvSize > 0) break;

            // 짧은 시간 대기
            Sleep(10);
        }

        if (recvSize <= 0) {
            AddLog(_T("PLC 응답 수신 실패: 타임아웃 또는 오류"));
            return FALSE;
        }

        // 디버깅 용 응답 패킷 로그
        strPacket = _T("수신 패킷: ");
        for (int i = 0; i < min(recvSize, 50); i++) {
            CString temp;

            temp.Format(_T("%02X "), recvBuffer[i]);
            strPacket += temp;
        }
        if (recvSize > 50) strPacket += _T("...");
        AddLog(strPacket);

        // 응답 헤더 확인 (LSIS-XGT)
        if (recvSize < 28 ||
            recvBuffer[0] != 0x4C || recvBuffer[1] != 0x53 || recvBuffer[2] != 0x49 || recvBuffer[3] != 0x53 ||
            recvBuffer[4] != 0x2D || recvBuffer[5] != 0x58 || recvBuffer[6] != 0x47 || recvBuffer[7] != 0x54) {

            AddLog(_T("PLC 응답: 잘못된 헤더 또는 응답 길이 부족"));
            return FALSE;
        }

        // 명령어 확인 (0x0055: 읽기 응답)
        if (recvBuffer[20] != 0x55 || recvBuffer[21] != 0x00) {
            AddLog(_T("PLC 응답: 잘못된 명령어 응답"));
            return FALSE;
        }

        // 에러 상태 확인 (26,27 바이트)
        WORD errorState = (recvBuffer[27] << 8) | recvBuffer[26];

        if (errorState != 0) {
            CString strError;
            strError.Format(_T("PLC 데이터 읽기 에러: 0x%04X"), errorState);
            AddLog(strError);

            // 에러 코드에 따른 상세 메시지
            switch (errorState) {
            case 0x0001:
                AddLog(_T("PLC 오류: 잘못된 메모리 주소 또는 액세스 권한 없음"));
                break;
            case 0x0002:
                AddLog(_T("PLC 오류: 범위 초과"));
                break;
            case 0x0003:
                AddLog(_T("PLC 오류: 데이터 크기 초과"));
                break;
            case 0x0004:
                AddLog(_T("PLC 오류: 요청 데이터 오류"));
                break;
            default:
                AddLog(_T("PLC 오류: 알 수 없는 오류 코드"));
                break;
            }

            return FALSE;
        }

        // 블록 수 확인
        if (recvSize < 30) {
            AddLog(_T("PLC 응답: 데이터 블록 정보 없음"));
            return FALSE;
        }

        WORD blockCount = (recvBuffer[29] << 8) | recvBuffer[28];

        if (blockCount == 0) {
            AddLog(_T("PLC 응답: 데이터 블록이 없음"));
            return FALSE;
        }

        // 데이터 크기 확인
        if (recvSize < 32) {
            AddLog(_T("PLC 응답: 데이터 크기 정보 없음"));
            return FALSE;
        }

        WORD dataSize = (recvBuffer[31] << 8) | recvBuffer[30];

        if (dataSize != 2 || recvSize < 34) {  // WORD 타입은 2바이트
            CString strError;
            strError.Format(_T("PLC 응답: 예상 데이터 크기 불일치 (크기: %d, 필요: 2)"), dataSize);
            AddLog(strError);
            return FALSE;
        }

        // 데이터 읽기 - 테스트 프로그램과 동일하게 처리
        // XGT 프로토콜은 리틀 엔디안으로 데이터 전송
        WORD commandValue = (recvBuffer[33] << 8) | recvBuffer[32];

        // 로그 출력
        strLog.Format(_T("PLC 주소 D%d에서 인디케이터 %d 명령 값 읽기 성공: 0x%04X (%d)"),
            plcAddress, indicatorIndex + 1, commandValue, commandValue);
        AddLog(strLog);

        // 명령이 0이 아닌 경우 모든 인디케이터로 전송
        if (commandValue != 0) {
            AddLog(_T("영점 명령 발견! 모든 인디케이터에 영점 명령을 전송합니다."));

            // 모든 인디케이터에 영점 명령 전송
            for (int i = 0; i < m_indicatorCount; i++) {
                if (m_indicators[i].connected) {
                    SendCommandToIndicator(i, commandValue);
                }
            }


        }

        return TRUE;
    }
    catch (CException* e) {
        TCHAR szError[1024];
        e->GetErrorMessage(szError, 1024);
        CString strError;
        strError.Format(_T("PLC 데이터 읽기 중 예외 발생: %s"), szError);
        AddLog(strError);
        e->Delete();
        return FALSE;
    }
}

// 인디케이터에 명령을 보내는 함수
BOOL CMy0430MFCAppDlg::SendCommandToIndicator(int indicatorIndex, WORD command)
{
    if (indicatorIndex < 0 || indicatorIndex >= m_indicatorCount || !m_indicators[indicatorIndex].connected) {
        return FALSE;
    }

    IndicatorInfo& indicator = m_indicators[indicatorIndex];

    // ModBus TCP 요청 패킷 구성 (Write Single Register, 함수 코드 0x06)
    BYTE requestPacket[12];

    // MBAP 헤더
    WORD transactionId = g_transactionId++;
    requestPacket[0] = (transactionId >> 8) & 0xFF;  // Transaction ID High
    requestPacket[1] = transactionId & 0xFF;         // Transaction ID Low
    requestPacket[2] = 0x00;                         // Protocol ID High
    requestPacket[3] = 0x00;                         // Protocol ID Low
    requestPacket[4] = 0x00;                         // Length High
    requestPacket[5] = 0x06;                         // Length Low (6 바이트)
    requestPacket[6] = 0x01;                         // Unit ID

    // PDU - Write Single Register (0x06)
    requestPacket[7] = 0x06;                         // Function Code
    requestPacket[8] = 0x00;                         // Register Address High
    requestPacket[9] = 0x40;                         // Register Address Low (외부입력명령 레지스터 0x40)
    requestPacket[10] = (command >> 8) & 0xFF;       // Register Value High
    requestPacket[11] = command & 0xFF;              // Register Value Low

    // 요청 전송
    if (!indicator.socket.SendData(requestPacket, sizeof(requestPacket))) {
        CString strError;
        strError.Format(_T("인디케이터 %d에 명령 전송 실패"), indicatorIndex + 1);
        AddLog(strError);
        return FALSE;
    }

    CString strLog;
    strLog.Format(_T("인디케이터 %d에 명령 0x%04X 전송"), indicatorIndex + 1, command);
    AddLog(strLog);

    return TRUE;
}
BOOL CMy0430MFCAppDlg::WriteAllIndicatorValuesToPLCContinuous()
{
    if (!m_plcSocket.IsConnected()) {
        AddLog(_T("PLC에 연결되어 있지 않아 데이터를 쓸 수 없습니다."));
        return FALSE;
    }

    try {
        // XGT 전용 프로토콜 패킷 구성 - 연속 쓰기(블록 단위)
        // 1. Company ID + 헤더 (20 바이트)
        BYTE header[20] = {
            0x4C, 0x53, 0x49, 0x53, 0x2D, 0x58, 0x47, 0x54, 0x00, 0x00, // "LSIS-XGT"
            0x00, 0x00,  // PLC Info
            0xA0,        // CPU Info
            0x33,        // Source of Frame (0x33: Client->Server)
            0x00, 0x0A,  // Invoke ID (임의 값)
            0x00, 0x00,  // Length (나중에 설정)
            0x00,        // FEnet Position
            0x00         // Reserved
        };

        // 2. 명령어 (Block Write Request: 0x005C)
        BYTE command[2] = { 0x5C, 0x00 };

        // 3. 데이터 타입 (Word: 0x0002)
        BYTE dataType[2] = { 0x02, 0x00 };

        // 4. 예약 영역
        BYTE reserved[2] = { 0x00, 0x00 };

        // 5. 시작 변수 이름 (첫 번째 인디케이터의 PLC 주소)
        CString strStartAddr;
        strStartAddr.Format(_T("%%DW6001"));  // 시작 주소는 항상 D6001
        CT2CA pszStartAddr(strStartAddr);
        int startAddrLen = strlen(pszStartAddr);
        BYTE startAddrLength[2] = { (BYTE)startAddrLen, 0x00 };

        // 6. 시작 변수 이름 문자열
        std::vector<BYTE> startAddrName(startAddrLen);
        memcpy(startAddrName.data(), pszStartAddr, startAddrLen);

        // 7. 데이터 개수 (인디케이터 수 * 10) - 각 인디케이터마다 10워드씩 할당
        int dataCount = m_indicatorCount * 10;
        BYTE dataCountBytes[2] = { (BYTE)(dataCount & 0xFF), (BYTE)((dataCount >> 8) & 0xFF) };

        // 8. 데이터 크기 (워드 수 * 2)
        int dataSize = dataCount * 2;  // 각 워드는 2바이트
        BYTE dataSizeBytes[2] = { (BYTE)(dataSize & 0xFF), (BYTE)((dataSize >> 8) & 0xFF) };

        // 9. 데이터 - 모든 인디케이터의 측정값을 연속해서 구성
        std::vector<BYTE> dataBuffer(dataSize, 0);  // 모든 바이트 0으로 초기화

        // 연결된 인디케이터의 측정값을 해당 위치에 설정
        for (int i = 0; i < m_indicatorCount; i++) {
            if (m_indicators[i].connected) {
                // 각 인디케이터의 데이터는 10워드 간격으로 저장
                // 측정값은 첫 번째 워드에 저장 (D6001, D6011, D6021, ...)
                int offset = i * 20;  // 워드 위치 * 2바이트

                // 측정값 (16비트) 저장
                dataBuffer[offset] = (m_indicators[i].measuredValue >> 0) & 0xFF;  // 최하위 바이트
                dataBuffer[offset + 1] = (m_indicators[i].measuredValue >> 8) & 0xFF;  // 상위 바이트
            }
        }

        // 총 패킷 길이 계산
        int dataLength = sizeof(command) + sizeof(dataType) + sizeof(reserved) +
            sizeof(startAddrLength) + startAddrLen +
            sizeof(dataCountBytes) + sizeof(dataSizeBytes) + dataBuffer.size();

        // 헤더의 Length 필드 업데이트
        header[16] = dataLength & 0xFF;
        header[17] = (dataLength >> 8) & 0xFF;

        // 전체 패킷 구성
        std::vector<BYTE> packet;
        packet.insert(packet.end(), header, header + sizeof(header));
        packet.insert(packet.end(), command, command + sizeof(command));
        packet.insert(packet.end(), dataType, dataType + sizeof(dataType));
        packet.insert(packet.end(), reserved, reserved + sizeof(reserved));
        packet.insert(packet.end(), startAddrLength, startAddrLength + sizeof(startAddrLength));
        packet.insert(packet.end(), startAddrName.begin(), startAddrName.end());
        packet.insert(packet.end(), dataCountBytes, dataCountBytes + sizeof(dataCountBytes));
        packet.insert(packet.end(), dataSizeBytes, dataSizeBytes + sizeof(dataSizeBytes));
        packet.insert(packet.end(), dataBuffer.begin(), dataBuffer.end());

        // 요청 전송
        if (!m_plcSocket.SendData(packet.data(), packet.size())) {
            CString strError;
            strError.Format(_T("PLC에 인디케이터 측정값 연속 쓰기 실패"));
            AddLog(strError);
            return FALSE;
        }

        CString strLog;
        strLog.Format(_T("모든 인디케이터 측정값을 PLC 주소 D6001부터 연속으로 쓰기 요청 (XGT 프로토콜, 블록 쓰기, %d 워드)"),
            dataCount);
        AddLog(strLog);

        return TRUE;
    }
    catch (CException* e) {
        TCHAR szError[1024];
        e->GetErrorMessage(szError, 1024);
        CString strError;
        strError.Format(_T("PLC 연속 데이터 쓰기 중 예외 발생: %s"), szError);
        AddLog(strError);
        e->Delete();
        return FALSE;
    }
}



BOOL CMy0430MFCAppDlg::ExecuteSequentialOperation()
{
    // 작업 상태 관리 - 3단계로 간단하게 구성
    // 0: PLC 명령 읽기
    // 1: 모든 인디케이터 데이터 읽기
    // 2: 모든 인디케이터 데이터를 PLC에 쓰기

    // 작업 번호가 최대 작업 수를 초과하면 초기화
    if (m_nCurrentOperation >= 3) {
        m_nCurrentOperation = 0;
    }

    // PLC 연결 상태 먼저 확인
    if (!m_plcSocket.IsConnected() && m_bPolling) {
        CString strLog;
        strLog.Format(_T("PLC 연결이 끊어짐. 재연결 시도: %s:%d"), m_strPLCIP, m_nPLCPort);
        AddLog(strLog);
        ConnectToPLC(m_strPLCIP, m_nPLCPort);
        return TRUE;
    }

    // 단계별 작업 수행
    switch (m_nCurrentOperation) {
    case 0: // PLC 명령 읽기
        if (m_plcSocket.IsConnected()) {
            ReadCommandFromPLCToIndicator(0);
        }
        break;

    case 1: // 모든 인디케이터 데이터 읽기
        ReadAllIndicatorData();
        break;

    case 2: // 모든 인디케이터 데이터를 PLC에 쓰기
        WriteAllIndicatorValuesToPLCContinuous();
        break;
    }

    // 다음 작업으로 이동
    m_nCurrentOperation = (m_nCurrentOperation + 1) % 3;

    return TRUE;
}
// 일별 로그 파일 생성 함수
// 일별 로그 파일 생성 함수
void CMy0430MFCAppDlg::CreateDailyLogFile()
{
    // 현재 날짜로 파일 이름 생성
    CTime currentTime = CTime::GetCurrentTime();
    CString strFileName;
    strFileName.Format(_T("Log_%04d%02d%02d.txt"),
        currentTime.GetYear(), currentTime.GetMonth(), currentTime.GetDay());

    // 실행 파일 경로 구하기
    TCHAR szPath[MAX_PATH];
    GetModuleFileName(NULL, szPath, MAX_PATH);
    CString strPath(szPath);
    int nPos = strPath.ReverseFind('\\');
    if (nPos > 0)
        strPath = strPath.Left(nPos + 1);

    // 로그 폴더 생성
    CString strLogFolder = strPath + _T("Logs");
    CreateDirectory(strLogFolder, NULL);

    // 로그 파일 경로 설정
    m_strLogFilePath = strLogFolder + _T("\\") + strFileName;

    // 파일이 없으면 헤더 추가
    BOOL bFileExists = PathFileExists(m_strLogFilePath);

    if (!bFileExists) {
        // Windows API를 사용하여 UTF-8로 파일 생성
        HANDLE hFile = CreateFile(
            m_strLogFilePath,
            GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        if (hFile != INVALID_HANDLE_VALUE) {
            // UTF-8 BOM 추가
            BYTE bom[] = { 0xEF, 0xBB, 0xBF };
            DWORD bytesWritten = 0;
            WriteFile(hFile, bom, sizeof(bom), &bytesWritten, NULL);

            // 헤더 문자열 구성
            CString strHeader;
            strHeader.Format(
                _T("==========================================================\r\n")
                _T("             인디케이터 통신 프로그램 로그\r\n")
                _T("==========================================================\r\n")
                _T("시작 시간: %04d-%02d-%02d %02d:%02d:%02d\r\n\r\n"),
                currentTime.GetYear(), currentTime.GetMonth(), currentTime.GetDay(),
                currentTime.GetHour(), currentTime.GetMinute(), currentTime.GetSecond());

            // CString을 UTF-8로 변환
            int nLen = WideCharToMultiByte(CP_UTF8, 0, strHeader, -1, NULL, 0, NULL, NULL);
            if (nLen > 0) {
                char* pszUTF8 = new char[nLen];
                WideCharToMultiByte(CP_UTF8, 0, strHeader, -1, pszUTF8, nLen, NULL, NULL);

                // 파일에 쓰기
                WriteFile(hFile, pszUTF8, nLen - 1, &bytesWritten, NULL); // -1은 NULL 종료 문자 제외
                delete[] pszUTF8;
            }

            CloseHandle(hFile);
        }
        else {
            DWORD dwError = GetLastError();
            CString strError;
            strError.Format(_T("로그 파일 생성 오류: %d"), dwError);
            AfxMessageBox(strError);
        }
    }

    // 프로그램 시작 로그 추가
    CString strLog;
    strLog.Format(_T("프로그램 시작 - 버전 1.3.1 (%04d-%02d-%02d %02d:%02d:%02d)"),
        currentTime.GetYear(), currentTime.GetMonth(), currentTime.GetDay(),
        currentTime.GetHour(), currentTime.GetMinute(), currentTime.GetSecond());

    SaveLogToFile(strLog);
}

// 로그를 파일에 저장하는 함수
void CMy0430MFCAppDlg::SaveLogToFile(LPCTSTR pszLog)
{
    // 현재 날짜 확인
    CTime currentTime = CTime::GetCurrentTime();
    CString strExpectedFileName;
    strExpectedFileName.Format(_T("Log_%04d%02d%02d.txt"),
        currentTime.GetYear(), currentTime.GetMonth(), currentTime.GetDay());

    // 파일 이름에서 날짜 부분 추출
    CString strCurrentFileName = m_strLogFilePath.Right(m_strLogFilePath.GetLength() - m_strLogFilePath.ReverseFind('\\') - 1);

    // 날짜가 변경되었는지 확인
    if (strCurrentFileName != strExpectedFileName) {
        // 새로운 날짜의 로그 파일 생성
        CreateDailyLogFile();
    }

    // Windows API를 사용하여 UTF-8로 파일 열기
    HANDLE hFile = CreateFile(
        m_strLogFilePath,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hFile != INVALID_HANDLE_VALUE) {
        // 파일 끝으로 이동
        SetFilePointer(hFile, 0, NULL, FILE_END);

        // 포맷 변경: [시간] 내용 형식으로
        CString strFormattedLog;
        strFormattedLog.Format(_T("[%02d:%02d:%02d] %s\r\n"),
            currentTime.GetHour(), currentTime.GetMinute(), currentTime.GetSecond(), pszLog);

        // CString을 UTF-8로 변환
        int nLen = WideCharToMultiByte(CP_UTF8, 0, strFormattedLog, -1, NULL, 0, NULL, NULL);
        if (nLen > 0) {
            char* pszUTF8 = new char[nLen];
            WideCharToMultiByte(CP_UTF8, 0, strFormattedLog, -1, pszUTF8, nLen, NULL, NULL);

            // 파일에 쓰기
            DWORD bytesWritten = 0;
            WriteFile(hFile, pszUTF8, nLen - 1, &bytesWritten, NULL); // -1은 NULL 종료 문자 제외
            delete[] pszUTF8;
        }

        CloseHandle(hFile);
    }
    else {
        DWORD dwError = GetLastError();
        CString strError;
        strError.Format(_T("로그 파일 열기 오류: %d"), dwError);
        TRACE(_T("%s\n"), strError);
    }
}


// 로그 항목 수 제한 함수
void CMy0430MFCAppDlg::LimitLogItems()
{
    // 현재 로그 항목 수 확인
    int nCount = m_logList.GetCount();

    // 현재 선택된 항목과 표시 위치 저장
    int nTopIndex = m_logList.GetTopIndex();
    bool bAtBottom = false;

    // 현재 스크롤이 맨 아래에 있는지 확인
    // 리스트박스의 높이를 기준으로 대략적으로 판단
    CRect rect;
    m_logList.GetClientRect(&rect);
    int itemHeight = m_logList.GetItemHeight(0);
    if (itemHeight > 0) {
        int visibleItems = rect.Height() / itemHeight;
        bAtBottom = (nTopIndex + visibleItems >= nCount);
    }

    // 최대 항목 수를 초과하면 오래된 항목부터 삭제
    if (nCount > MAX_LOG_ITEMS) {
        int nDeleteCount = nCount - MAX_LOG_ITEMS;
        for (int i = 0; i < nDeleteCount; i++) {
            m_logList.DeleteString(0);  // 항상 가장 첫 번째 항목 삭제
        }

        // 삭제 후 이전 위치 복원 (삭제된 항목 수 고려)
        if (bAtBottom) {
            // 맨 아래에 있었으면 계속 맨 아래에 유지
            int newCount = m_logList.GetCount();
            // GetItemRect 또는 GetItemHeight를 사용하여 계산된 항목 수를 계산
            CRect rect;
            m_logList.GetClientRect(&rect);
            int itemHeight = m_logList.GetItemHeight(0);
            if (itemHeight > 0) {
                int visibleItems = rect.Height() / itemHeight;
                m_logList.SetTopIndex(newCount > visibleItems ? newCount - visibleItems : 0);
            }
        }
        else {
            // 스크롤 위치 조정 (삭제된 항목 수만큼)
            m_logList.SetTopIndex(nTopIndex > nDeleteCount ? nTopIndex - nDeleteCount : 0);
        }
    }
}
// 인디케이터 오류 상태를 PLC에 보고하는 함수
BOOL CMy0430MFCAppDlg::ReportIndicatorErrorToPLC(int indicatorIndex, bool bError)
{
    if (!m_plcSocket.IsConnected()) {
        return FALSE;
    }

    // 오류 상태 변화 추적
    static bool previousErrorState[MAX_INDICATORS] = { false };

    // 오류가 발생하거나 복구되었을 때만 로그 출력
    if (bError != previousErrorState[indicatorIndex]) {
        CString strLog;
        if (bError) {
            strLog.Format(_T("인디케이터 %d 오류 발생, PLC에 보고됨"),
                indicatorIndex + 1);
        }
        else {
            strLog.Format(_T("인디케이터 %d 오류 복구됨, PLC에 보고됨"),
                indicatorIndex + 1);
        }
        AddLog(strLog);
    }

    // 현재 상태 저장
    previousErrorState[indicatorIndex] = bError;

    // PLC 메모리 주소 계산 (D6002 + 인디케이터 인덱스 * 10)
    WORD plcAddress = 6002 + (indicatorIndex * 10);

    // XGT 전용 프로토콜 패킷 구성 - 쓰기 요청
    BYTE header[20] = {
        0x4C, 0x53, 0x49, 0x53, 0x2D, 0x58, 0x47, 0x54, 0x00, 0x00, // "LSIS-XGT"
        0x00, 0x00,  // PLC Info
        0xA0,        // CPU Info
        0x33,        // Source of Frame (0x33: Client->Server)
        0x00, 0x04,  // Invoke ID (임의 값)
        0x00, 0x00,  // Length (나중에 설정)
        0x00,        // FEnet Position
        0x00         // Reserved
    };

    BYTE command[2] = { 0x58, 0x00 };  // Write Request
    BYTE dataType[2] = { 0x02, 0x00 }; // Word 타입
    BYTE reserved[2] = { 0x00, 0x00 };
    BYTE varCount[2] = { 0x01, 0x00 };

    CString strVarName;
    strVarName.Format(_T("%%DW%d"), plcAddress);
    CT2CA pszVarName(strVarName);
    int varNameLen = strlen(pszVarName);
    BYTE varNameLength[2] = { (BYTE)varNameLen, 0x00 };

    std::vector<BYTE> varName(varNameLen);
    memcpy(varName.data(), pszVarName, varNameLen);

    BYTE dataSize[2] = { 0x02, 0x00 };

    // 오류면 1, 정상이면 0
    BYTE data[2];
    data[0] = bError ? 0x01 : 0x00;
    data[1] = 0x00;

    // 총 패킷 길이 계산
    int dataLength = sizeof(command) + sizeof(dataType) + sizeof(reserved) +
        sizeof(varCount) + sizeof(varNameLength) + varNameLen +
        sizeof(dataSize) + sizeof(data);

    header[16] = dataLength & 0xFF;
    header[17] = (dataLength >> 8) & 0xFF;

    // 전체 패킷 구성
    std::vector<BYTE> packet;
    packet.insert(packet.end(), header, header + sizeof(header));
    packet.insert(packet.end(), command, command + sizeof(command));
    packet.insert(packet.end(), dataType, dataType + sizeof(dataType));
    packet.insert(packet.end(), reserved, reserved + sizeof(reserved));
    packet.insert(packet.end(), varCount, varCount + sizeof(varCount));
    packet.insert(packet.end(), varNameLength, varNameLength + sizeof(varNameLength));
    packet.insert(packet.end(), varName.begin(), varName.end());
    packet.insert(packet.end(), dataSize, dataSize + sizeof(dataSize));
    packet.insert(packet.end(), data, data + sizeof(data));

    // 요청 전송
    if (!m_plcSocket.SendData(packet.data(), packet.size())) {
        CString strError;
        strError.Format(_T("인디케이터 %d 오류 상태 보고 실패"), indicatorIndex + 1);
        AddLog(strError);
        return FALSE;
    }

   

    return TRUE;
}

// PLC 하트비트 시작 함수 추가
void CMy0430MFCAppDlg::StartPLCHeartbeat()
{
    // 1초(1000ms) 간격으로 하트비트 타이머 설정
    m_plcHeartbeatTimerId = SetTimer(2, 1000, NULL);
    m_bHeartbeatValue = false;  // 초기값 0

    AddLog(_T("PLC 하트비트 전송 시작"));
}

// PLC 하트비트 중지 함수 추가
void CMy0430MFCAppDlg::StopPLCHeartbeat()
{
    if (m_plcHeartbeatTimerId != 0) {
        KillTimer(m_plcHeartbeatTimerId);
        m_plcHeartbeatTimerId = 0;
        AddLog(_T("PLC 하트비트 전송 중지"));
    }
}

// PLC 하트비트 쓰기 함수 추가
BOOL CMy0430MFCAppDlg::WritePLCHeartbeat()
{
    if (!m_plcSocket.IsConnected()) {
        return FALSE;
    }

    // 값 토글 (0과 1을 번갈아 사용)
    m_bHeartbeatValue = !m_bHeartbeatValue;

    // PLC 메모리 주소 D6004
    WORD plcAddress = 6004;

    // XGT 전용 프로토콜 패킷 구성
    BYTE header[20] = {
        0x4C, 0x53, 0x49, 0x53, 0x2D, 0x58, 0x47, 0x54, 0x00, 0x00, // "LSIS-XGT"
        0x00, 0x00,  // PLC Info
        0xA0,        // CPU Info
        0x33,        // Source of Frame (0x33: Client->Server)
        0x00, 0x06,  // Invoke ID (하트비트용 고유 ID)
        0x00, 0x00,  // Length (나중에 설정)
        0x00,        // FEnet Position
        0x00         // Reserved
    };

    BYTE command[2] = { 0x58, 0x00 };  // Write Request
    BYTE dataType[2] = { 0x02, 0x00 }; // Word 타입
    BYTE reserved[2] = { 0x00, 0x00 };
    BYTE varCount[2] = { 0x01, 0x00 };

    CString strVarName;
    strVarName.Format(_T("%%DW%d"), plcAddress);
    CT2CA pszVarName(strVarName);
    int varNameLen = strlen(pszVarName);
    BYTE varNameLength[2] = { (BYTE)varNameLen, 0x00 };

    std::vector<BYTE> varName(varNameLen);
    memcpy(varName.data(), pszVarName, varNameLen);

    BYTE dataSize[2] = { 0x02, 0x00 };

    // 하트비트 값 (0 또는 1)
    BYTE data[2];
    data[0] = m_bHeartbeatValue ? 0x01 : 0x00;
    data[1] = 0x00;

    // 총 패킷 길이 계산
    int dataLength = sizeof(command) + sizeof(dataType) + sizeof(reserved) +
        sizeof(varCount) + sizeof(varNameLength) + varNameLen +
        sizeof(dataSize) + sizeof(data);

    header[16] = dataLength & 0xFF;
    header[17] = (dataLength >> 8) & 0xFF;

    // 전체 패킷 구성
    std::vector<BYTE> packet;
    packet.insert(packet.end(), header, header + sizeof(header));
    packet.insert(packet.end(), command, command + sizeof(command));
    packet.insert(packet.end(), dataType, dataType + sizeof(dataType));
    packet.insert(packet.end(), reserved, reserved + sizeof(reserved));
    packet.insert(packet.end(), varCount, varCount + sizeof(varCount));
    packet.insert(packet.end(), varNameLength, varNameLength + sizeof(varNameLength));
    packet.insert(packet.end(), varName.begin(), varName.end());
    packet.insert(packet.end(), dataSize, dataSize + sizeof(dataSize));
    packet.insert(packet.end(), data, data + sizeof(data));

    // 요청 전송
    if (!m_plcSocket.SendData(packet.data(), packet.size())) {
        CString strError;
        strError.Format(_T("PLC 하트비트 전송 실패 (D%d에 %d 쓰기)"), plcAddress, m_bHeartbeatValue ? 1 : 0);
        AddLog(strError);
        return FALSE;
    }

    CString strLog;
    strLog.Format(_T("PLC 하트비트 전송: D%d에 %d 쓰기"), plcAddress, m_bHeartbeatValue ? 1 : 0);
    AddLog(strLog);

    return TRUE;
}