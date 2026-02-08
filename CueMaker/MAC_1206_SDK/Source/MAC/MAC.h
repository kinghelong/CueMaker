#pragma once

class CFormatArray;
class CMACSettings;
class CMACDlg;

#pragma warning(push)
#include <gdiplus.h>
#pragma warning(pop)

#include "resource.h"
#include "GDIBitmapPtr.h"

class CMACApp : public CWinApp
{
public:
    // construction / destruction
    CMACApp();
    ~CMACApp();

    // initialize
    virtual BOOL InitInstance();
    virtual int ExitInstance();

    // data access
    CFormatArray * GetFormatArray();
    CMACSettings * GetSettings();
    enum EImageList
    {
        Image_Toolbar,
        Image_OptionsList,
        Image_OptionsPages
    };
    void DeleteImageLists();
    CImageList * GetImageList(EImageList Image);
    int GetSize(int nSize, double dAdditional = 1.0) const;
    int GetSizeReverse(int nSize) const;
    double GetScale() const { return m_dScale; }
    CFont * GetFont() { return &m_Font; }
    bool SetScale(double dScale, bool bForce = false);
    Gdiplus::Bitmap * GetMonkeyImage();
    void LoadFont(bool bRebuild);
    UINT & GetMonitorDPI() { return m_nMonitorDPI; }

    // message map
    DECLARE_MESSAGE_MAP()

private:
    // helper objects
    APE::CSmartPtr<CFormatArray> m_sparyFormats;
    APE::CSmartPtr<CMACSettings> m_spSettings;
    CImageList m_ImageListToolbar;
    CImageList m_ImageListOptionsList;
    CImageList m_ImageListOptionsPages;
    CGDIBitmapPtr m_spbmpButtons;
    CGDIBitmapPtr m_spbmpMonkey;
    double m_dScale;
    HANDLE m_hSingleInstance;
    bool m_bAnotherInstanceRunning;
    CMACDlg * m_pMACDlg;
    CFont m_Font;
    UINT m_nMonitorDPI;
};

extern CMACApp theApp;
