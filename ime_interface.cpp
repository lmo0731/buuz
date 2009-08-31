/*
 * Copyright 2009 Odbayar Nyamtseren <odbayar.n@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <windows.h>
#include <immdev.h>
#include <string.h>
#include <tchar.h>
#include "common.h"
#include "input_context.h"
#include "comp_string.h"
#include "composer.h"

namespace /* anonymous */
{
    void initContext(InputContext* imc)
    {
        INPUTCONTEXT* imcPtr = imc->ptr();

        if (!(imcPtr->fdwInit & INIT_CONVERSION))
        {
            imcPtr->fdwConversion = IME_CMODE_NATIVE;
            imcPtr->fdwInit |= INIT_CONVERSION;
        }

        if (!(imcPtr->fdwInit & INIT_LOGFONT))
        {
            NONCLIENTMETRICS ncMetrics;
            ncMetrics.cbSize = sizeof(NONCLIENTMETRICS);
            SystemParametersInfo(SPI_GETNONCLIENTMETRICS,
                    sizeof(NONCLIENTMETRICS), &ncMetrics, 0);
            imcPtr->lfFont.W = ncMetrics.lfMessageFont;
            imcPtr->fdwInit |= INIT_LOGFONT;
        }

        if (!(imcPtr->fdwInit & INIT_COMPFORM))
        {
            imcPtr->cfCompForm.dwStyle = CFS_POINT;
            imcPtr->cfCompForm.ptCurrentPos.x = 0;
            imcPtr->cfCompForm.ptCurrentPos.y = 0;
            if (IsWindow(imcPtr->hWnd))
                GetClientRect(imcPtr->hWnd, &imcPtr->cfCompForm.rcArea);
            else
                memset(&imcPtr->cfCompForm.rcArea, 0, sizeof(RECT));
            imcPtr->fdwInit |= INIT_COMPFORM;
        }
    }
} // anonymous namespace

bool isWinLogonProcess;

extern "C"
BOOL WINAPI ImeInquire(LPIMEINFO imeInfo,
                       LPTSTR uiWndClass,
                       DWORD systemInfo)
{
    if (!imeInfo)
        return FALSE;

    imeInfo->dwPrivateDataSize = sizeof(ImcPrivate);
    imeInfo->fdwProperty = IME_PROP_UNICODE | IME_PROP_KBD_CHAR_FIRST |
                           IME_PROP_IGNORE_UPKEYS | IME_PROP_AT_CARET;
    imeInfo->fdwConversionCaps = IME_CMODE_NATIVE | IME_CMODE_SOFTKBD;
    imeInfo->fdwSentenceCaps = 0;
    imeInfo->fdwUICaps = UI_CAP_SOFTKBD;
    imeInfo->fdwSCSCaps = 0;
    imeInfo->fdwSelectCaps = 0;

    _tcscpy(uiWndClass, uiClassName);

    isWinLogonProcess = systemInfo & IME_SYSINFO_WINLOGON;

    return TRUE;
}

extern "C"
DWORD WINAPI ImeConversionList(HIMC hImc,
                               LPCTSTR src,
                               LPCANDIDATELIST dest,
                               DWORD bufLen,
                               UINT flag)
{
    return 0; // We won't implement this API
}

extern "C"
BOOL WINAPI ImeConfigure(HKL kl, HWND wnd, DWORD mode, LPVOID data)
{
    // TODO

    return FALSE;
}

extern "C"
BOOL WINAPI ImeDestroy(UINT reserved)
{
    if (reserved != 0)
        return FALSE;
    return TRUE;
}

extern "C"
LRESULT WINAPI ImeEscape(HIMC hImc, UINT escape, LPVOID data)
{
    switch (escape)
    {
    case IME_ESC_QUERY_SUPPORT:
        if (data)
        {
            switch (*(UINT*)data)
            {
            case IME_ESC_QUERY_SUPPORT:
            //case IME_ESC_GETHELPFILENAME:
                return TRUE;
            }
        }
        return FALSE;
    case IME_ESC_GETHELPFILENAME:
        //{
        //    LPTSTR path = (LPTSTR)data;
        //    if (path && GetWindowsDirectory(path, MAX_PATH))
        //    {
        //        if (_tcscat_s(path, MAX_PATH, _T("\\Help\\buuz.chm")) == 0)
        //            return TRUE;
        //    }
        //}
        return FALSE;
    default:
        return FALSE;
    }
}

extern "C"
BOOL WINAPI ImeProcessKey(HIMC hImc,
                          UINT virtKey,
                          LPARAM lParam,
                          CONST LPBYTE keyState)
{
    InputContext imc(hImc);

    if (!imc.lock())
        return FALSE;

    WORD scanCode = HIWORD(lParam);
    WCHAR charCode;
    if (ToUnicode(virtKey, scanCode, keyState, &charCode, 1, 0) == 0)
        charCode = 0;
    return composer->processKey(&imc, virtKey, scanCode, charCode, keyState);
}

extern "C"
UINT WINAPI ImeToAsciiEx(UINT virtKey,
                         UINT scanCode,
                         CONST LPBYTE keyState,
                         LPTRANSMSGLIST transMsgList,
                         UINT state,
                         HIMC hImc)
{
    InputContext imc(hImc);

    if (!imc.lock())
        return 0;

    ImcPrivate* imcPrv = imc.prv();

    imcPrv->numMsgs = 0;
    imcPrv->msgList = transMsgList;
    composer->toAsciiEx(&imc, LOWORD(virtKey), scanCode, HIWORD(virtKey), keyState);
    imcPrv->msgList = NULL;

    return imcPrv->numMsgs;
}

extern "C"
BOOL WINAPI NotifyIME(HIMC hImc, DWORD action, DWORD index, DWORD value)
{
    InputContext imc(hImc);
    CompString cs(&imc);
    BOOL retValue = FALSE;

    if (!imc.lock())
        return retValue;

    switch (action)
    {
    case NI_CONTEXTUPDATED:
        switch (value)
        {
        case IMC_SETOPENSTATUS:
            {
                if (!imc.ptr()->fOpen && cs.lock() && cs.compStr.size() != 0)
                    composer->cancelComp(&imc, &cs);
                retValue = TRUE;
            }
            break;
        case IMC_SETCONVERSIONMODE:
        case IMC_SETSENTENCEMODE:
            break;
        case IMC_SETCANDIDATEPOS:
        case IMC_SETCOMPOSITIONFONT:
        case IMC_SETCOMPOSITIONWINDOW:
            retValue = FALSE;
            break;
        }
        break;

    case NI_COMPOSITIONSTR:
        if (cs.lock())
        {
            switch (index)
            {
            case CPS_COMPLETE:
                composer->finishComp(&imc, &cs);
                retValue = TRUE;
                break;
            case CPS_CONVERT:
                break;
            case CPS_REVERT:
                break;
            case CPS_CANCEL:
                composer->cancelComp(&imc, &cs);
                retValue = TRUE;
                break;
            }
        }
        break;

    // We don't support candidate list.
    case NI_OPENCANDIDATE:
    case NI_CLOSECANDIDATE:
    case NI_SELECTCANDIDATESTR:
    case NI_CHANGECANDIDATELIST:
    case NI_SETCANDIDATE_PAGESTART:
    case NI_SETCANDIDATE_PAGESIZE:
        retValue = FALSE;
        break;
    }

    return retValue;
}

extern "C"
BOOL WINAPI ImeSelect(HIMC hImc, BOOL select)
{
    InputContext imc(hImc);

    if (!imc.lock())
        return FALSE;

    INPUTCONTEXT* imcPtr = imc.ptr();
    ImcPrivate* imcPrv = imc.prv();

    if (select)
    {
        CompString cs(&imc);
        if (!cs.create())
            return FALSE;

        imcPtr->fOpen = TRUE;

        initContext(&imc);
    }
    else
    {
        imcPtr->fOpen = FALSE;
    }

    return TRUE;
}

extern "C"
BOOL WINAPI ImeSetActiveContext(HIMC hImc, BOOL activate)
{
    InputContext imc(hImc);
    CompString cs(&imc);

    if (!(imc.lock() && cs.lock()))
        return FALSE;

    if (activate)
        initContext(&imc);
    else
        if (cs.compStr.size() != 0)
            composer->finishComp(&imc, &cs);

    return TRUE;
}

extern "C"
BOOL WINAPI ImeSetCompositionString(HIMC hImc,
                                    DWORD index,
                                    LPVOID comp,
                                    DWORD compLen,
                                    LPVOID read,
                                    DWORD readLen)
{
    return FALSE;
}

//
// We won't support dictionary, so everything below this line is unimportant.
//

extern "C"
BOOL WINAPI ImeRegisterWord(LPCTSTR reading, DWORD style, LPCTSTR string)
{
    return FALSE;
}

extern "C"
BOOL WINAPI ImeUnregisterWord(LPCTSTR reading, DWORD style, LPCTSTR string)
{
    return FALSE;
}

extern "C"
UINT WINAPI ImeGetRegisterWordStyle(UINT item, LPSTYLEBUF styleBuf)
{
    return FALSE;
}

extern "C"
UINT WINAPI ImeEnumRegisterWord(REGISTERWORDENUMPROC enumProc,
        LPCTSTR reading, DWORD style, LPCTSTR string, LPVOID data)
{
    return FALSE;
}
