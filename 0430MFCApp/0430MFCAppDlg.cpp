// 0430MFCAppDlg.cpp: 구현 파일
#include "pch.h"
#include "framework.h"
#include "0430MFCApp.h"
#include "0430MFCAppDlg.h"
#include "afxdialogex.h"
#include <fstream>
#include <sstream>
#include <atlconv.h>

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
    , m_delayMs(1000) // 기본 딜레이 1초
    , m_bPolling(false)
    , m_timerId(0)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

CMy0430MFCAppDlg::~CMy0430MFCAppDlg()
{
    // 타이머 해제
    if (m_timerId != 0)
        KillTimer(m_timerId);

    // 소켓 연결 해제
    for (size_t i = 0; i < m_indicators.size(); i++)
    {
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

    m_indicators.clear();

    std::string line;
    // 헤더 라인 건너뛰기
    std::getline(file, line);

    // 인디케이터 정보 읽기
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string ip, port;

        if (std::getline(iss, ip, ',') && std::getline(iss, port, ',')) {
            IndicatorInfo info;
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
            info.socket.SetSocketID(m_indicators.size()); // 인덱스를 소켓 ID로 사용

            m_indicators.push_back(info);
        }
    }

    return !m_indicators.empty();
}

// 인디케이터 연결
BOOL CMy0430MFCAppDlg::ConnectToIndicator(int index)
{
    if (index < 0 || index >= (int)m_indicators.size()) {
        return FALSE;
    }

    IndicatorInfo& indicator = m_indicators[index];

    // 이미 연결되어 있으면 재연결
    if (indicator.connected) {
        DisconnectIndicator(index);
    }

    // 소켓 생성
    if (!indicator.socket.Create()) {
        CString strError;
        strError.Format(_T("인디케이터 %s:%d 소켓 생성 실패: 오류 코드 %d"),
            indicator.ip, indicator.port, GetLastError());
        AddLog(strError);
        return FALSE;
    }

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
    if (index < 0 || index >= (int)m_indicators.size()) {
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
BOOL CMy0430MFCAppDlg::ReadIndicatorData(int index)
{
    if (index < 0 || index >= (int)m_indicators.size() || !m_indicators[index].connected) {
        return FALSE;
    }

    IndicatorInfo& indicator = m_indicators[index];

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

    // 요청 전송
    CString strLog;
    strLog.Format(_T("인디케이터 %s:%d로 데이터 요청 전송"), indicator.ip, indicator.port);
    AddLog(strLog, requestPacket, sizeof(requestPacket));

    if (!indicator.socket.SendData(requestPacket, sizeof(requestPacket))) {
        CString strError;
        strError.Format(_T("인디케이터 %s:%d 데이터 요청 전송 실패"), indicator.ip, indicator.port);
        AddLog(strError);
        return FALSE;
    }

    return TRUE;
}

// 리스트 컨트롤 업데이트
void CMy0430MFCAppDlg::UpdateListControl()
{
    m_listIndicators.DeleteAllItems();

    for (size_t i = 0; i < m_indicators.size(); ++i) {
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
        // 모든 연결된 인디케이터에 데이터 요청
        for (int i = 0; i < (int)m_indicators.size(); ++i) {
            if (m_indicators[i].connected) {
                ReadIndicatorData(i);
            }
            else if (m_bPolling) {
                // 폴링 모드일 때 연결이 끊어진 인디케이터 재연결 시도
                ConnectToIndicator(i);
            }
        }
    }

    CDialogEx::OnTimer(nIDEvent);
}

// 연결 버튼 클릭
void CMy0430MFCAppDlg::OnBnClickedButtonConnect()
{
    UpdateData(TRUE);  // 컨트롤 -> 변수

    // 모든 인디케이터 연결
    bool allConnected = true;
    for (int i = 0; i < (int)m_indicators.size(); ++i) {
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
    for (int i = 0; i < (int)m_indicators.size(); ++i) {
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
    UpdateData(TRUE);  // 컨트롤 -> 변수

    // 타이머 설정
    m_timerId = SetTimer(1, m_delayMs, NULL);
    m_bPolling = true;

    // 로그
    CString strLog;
    strLog.Format(_T("데이터 폴링 시작: %d ms 간격"), m_delayMs);
    AddLog(strLog);

    // 버튼 상태 업데이트
    m_btnStartPolling.EnableWindow(FALSE);
    m_btnStopPolling.EnableWindow(TRUE);
}

// 폴링 중지 버튼 클릭
void CMy0430MFCAppDlg::OnBnClickedButtonStopPolling()
{
    // 타이머 중지
    KillTimer(m_timerId);
    m_timerId = 0;
    m_bPolling = false;

    // 로그
    AddLog(_T("데이터 폴링 중지"));

    // 버튼 상태 업데이트
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
    for (int i = 0; i < (int)m_indicators.size(); ++i) {
        DisconnectIndicator(i);
    }

    CDialogEx::OnClose();
}

// 소켓 연결 메시지 처리
LRESULT CMy0430MFCAppDlg::OnSocketConnect(WPARAM wParam, LPARAM lParam)
{
    int nSocketID = (int)wParam;
    int nErrorCode = (int)lParam;

    if (nSocketID >= 0 && nSocketID < (int)m_indicators.size()) {
        IndicatorInfo& indicator = m_indicators[nSocketID];

        if (nErrorCode == 0) {
            indicator.connected = true;

            CString strLog;
            strLog.Format(_T("인디케이터 %s:%d 연결 성공"), indicator.ip, indicator.port);
            AddLog(strLog);
        }
        else {
            indicator.connected = false;

            CString strError;
            strError.Format(_T("인디케이터 %s:%d 연결 실패: 오류 코드 %d"),
                indicator.ip, indicator.port, nErrorCode);
            AddLog(strError);
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

    if (nSocketID >= 0 && nSocketID < (int)m_indicators.size()) {
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

    if (nSocketID >= 0 && nSocketID < (int)m_indicators.size()) {
        IndicatorInfo& indicator = m_indicators[nSocketID];

        if (indicator.connected) {
            indicator.connected = false;

            CString strLog;
            strLog.Format(_T("인디케이터 %s:%d 연결 종료됨"), indicator.ip, indicator.port);
            AddLog(strLog);

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

    int nIndex = m_logList.AddString(strTimedLog);
    m_logList.SetTopIndex(nIndex);
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