// 0430MFCAppDlg.h: 헤더 파일
#pragma once
#include <vector>
#include <string>
#include <afxsock.h>

// 사용자 정의 메시지
#define WM_SOCKET_CONNECT (WM_USER + 1)
#define WM_SOCKET_RECEIVE (WM_USER + 2)
#define WM_SOCKET_CLOSE   (WM_USER + 3)

// ModbusTcp 소켓 클래스
class CModbusTcpSocket : public CAsyncSocket
{
public:
    CModbusTcpSocket();
    virtual ~CModbusTcpSocket();

    void SetParent(CWnd* pParent) { m_pParent = pParent; }
    void SetSocketID(int nID) { m_nSocketID = nID; }
    bool IsConnected() const { return m_bConnected; }

    BOOL SendData(const BYTE* pData, int nLength);

    // 받은 데이터 버퍼
    std::vector<BYTE> m_RecvBuffer;

protected:
    virtual void OnConnect(int nErrorCode);
    virtual void OnReceive(int nErrorCode);
    virtual void OnClose(int nErrorCode);
    virtual void OnSend(int nErrorCode);

private:
    CWnd* m_pParent;
    int m_nSocketID;
    bool m_bConnected;
};

// 인디케이터 정보 구조체
struct IndicatorInfo {
    CString ip;
    int port;
    bool connected;
    CModbusTcpSocket socket;

    // 데이터
    int maxCapacity;
    int minDivision;
    int decimalPoint;
    int adcValue;
    int measuredValue;
    int lampStatus;
    int errorData;
};

// CMy0430MFCAppDlg 대화 상자
class CMy0430MFCAppDlg : public CDialogEx
{
    // 생성입니다.

public:
    CStatic m_staticPLCStatus;// PLC 상태 표시용 static 컨트롤
    CString m_strPLCIP;   // PLC IP 주소
    int m_nPLCPort;       // PLC 포트
    CEdit m_editPLCIP;    // IP 입력 컨트롤
    CEdit m_editPLCPort;  // 포트 입력 컨트롤

    BOOL ConnectToPLC(const CString& ipAddress, int port);
    void DisconnectPLC();
    afx_msg void OnBnClickedButtonConnectPlc();
public:
    BOOL ReportIndicatorErrorToPLC(int indicatorIndex, bool bError);
    BOOL WriteIndicatorValueToPLC(int indicatorIndex);
    BOOL ReadCommandFromPLCToIndicator(int indicatorIndex);
    BOOL SendCommandToIndicator(int indicatorIndex, WORD command);

    CMy0430MFCAppDlg(CWnd* pParent = nullptr);	// 표준 생성자입니다.
    virtual ~CMy0430MFCAppDlg();

    // 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_MY0430MFCAPP_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 지원입니다.

    // 구현입니다.

protected:
    HICON m_hIcon;
    CListCtrl m_listIndicators;      // 인디케이터 데이터 표시용 리스트 컨트롤
    CEdit m_editDelay;               // 통신 딜레이 입력용 에디트 컨트롤
    CButton m_btnConnect;            // 연결 버튼
    CButton m_btnDisconnect;         // 연결 해제 버튼
    CButton m_btnStartPolling;       // 폴링 시작 버튼
    CButton m_btnStopPolling;        // 폴링 중지 버튼
    CListBox m_logList;              // 로그 표시용 리스트 박스

    // 고정 크기 배열로 변경
    static const int MAX_INDICATORS = 30;  // 최대 연결 개수
    IndicatorInfo m_indicators[MAX_INDICATORS];
    int m_indicatorCount;                 // 실제 사용 중인 인디케이터 개수

    int m_delayMs;                        // 통신 딜레이(ms)
    bool m_bPolling;                      // 폴링 중 여부
    UINT_PTR m_timerId;                   // 타이머 ID

    // 인디케이터 CSV 파일 읽기
    BOOL LoadIndicatorSettings(const CString& filePath);

    // ModBus TCP 통신 함수
    BOOL ConnectToIndicator(int index);
    void DisconnectIndicator(int index);
    BOOL ReadIndicatorData(int index);
    void UpdateListControl();

    // 로그 추가 함수
    void AddLog(LPCTSTR pszLog);
    void AddLog(LPCTSTR pszPrefix, const BYTE* pData, int nLength);

    // ModBus 패킷 분석
    CString AnalyzeModbusPacket(const BYTE* pData, int nLength);

    // 타이머 핸들러
    afx_msg void OnTimer(UINT_PTR nIDEvent);

    // 생성된 메시지 맵 함수
    virtual BOOL OnInitDialog();
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnBnClickedButtonConnect();
    afx_msg void OnBnClickedButtonDisconnect();
    afx_msg void OnBnClickedButtonStartPolling();
    afx_msg void OnBnClickedButtonStopPolling();
    afx_msg void OnBnClickedButtonClearLog();
    afx_msg void OnClose();

    // 소켓 메시지 핸들러
    afx_msg LRESULT OnSocketConnect(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnSocketReceive(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnSocketClose(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()
private:

    int m_nCurrentOperation;   // 현재 실행 중인 작업 번호 (0-23)
    BOOL ExecuteSequentialOperation(); // 순차 작업 실행 함수

    CModbusTcpSocket m_plcSocket; // PLC 전용 소켓
private:
    static const int MAX_LOG_ITEMS = 1000;  // 최대 로그 항목 수
    CString m_strLogFilePath;               // 로그 파일 경로

    void SaveLogToFile(LPCTSTR pszLog);     // 로그를 파일에 저장하는 함수
    void CreateDailyLogFile();              // 일별 로그 파일 생성 함수
    void LimitLogItems();                   // 로그 항목 수 제한 함수
private:
    UINT_PTR m_plcHeartbeatTimerId;    // PLC 하트비트 전용 타이머 ID
    bool m_bHeartbeatValue;            // 현재 하트비트 값 (true=1, false=0)

    // 함수 선언 추가  
private:
    BOOL WritePLCHeartbeat();          // PLC 하트비트 쓰기 함수
    void StartPLCHeartbeat();          // PLC 하트비트 시작
    void StopPLCHeartbeat();           // PLC 하트비트 중지
};

