
/* ---------------------------------------------------------------- 1.6: players and LAN games
   Players: accounts in BF-Portable's backend database (bf.a_emu_playerinfo), the table its website's Register page
   writes - same columns, same password hash (md5 of the password). Done through XAMPP's mysql.exe with the
   emulator's own database login (BFBC2-EMU\config.ini [database]), so no root password is needed.
   LAN games: looks on this PC's networks (/24 round each of its IPv4 addresses) for PCs running the BC2 emulator
   (Plasma client port 18390) and BC2 servers (RCON 47400-47403: "serverInfo" needs no login), plus the servers this
   PC's own backend lists. Join points the game's bfbc2.ini (host=) at that PC's emulator and starts the game; the
   server then shows in the game's server browser. */

/* the BF-Portable folder: from the emulator / MySQL paths in the launcher, else the usual places */
static int bfp_root(WCHAR *out)
{
    WCHAR p[MAX_PATH], t[MAX_PATH];
    get_text(ID_SVC + 20, p, MAX_PATH);   /* ...\BFBC2-EMU\mase_bc2.exe */
    if (p[0]) { wcscpy(t, p); WCHAR *s = wcsrchr(t, L'\\'); if (s) { *s = 0; s = wcsrchr(t, L'\\'); if (s) { *s = 0;
        swprintf(out, MAX_PATH, L"%ls", t); WCHAR c[MAX_PATH]; swprintf(c, MAX_PATH, L"%ls\\xampp\\mysql\\bin\\mysql.exe", out);
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) return 1; } } }
    get_text(ID_SVC, p, MAX_PATH);        /* ...\xampp\mysql_start.bat */
    if (p[0]) { wcscpy(t, p); WCHAR *s = wcsrchr(t, L'\\'); if (s) { *s = 0; s = wcsrchr(t, L'\\'); if (s) { *s = 0;
        swprintf(out, MAX_PATH, L"%ls", t); WCHAR c[MAX_PATH]; swprintf(c, MAX_PATH, L"%ls\\xampp\\mysql\\bin\\mysql.exe", out);
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) return 1; } } }
    static const WCHAR *roots[] = {L"D:\\BF-Portable", L"C:\\BF-Portable", L"E:\\BF-Portable"};
    for (unsigned r = 0; r < sizeof roots / sizeof *roots; r++) {
        WCHAR c[MAX_PATH]; swprintf(c, MAX_PATH, L"%ls\\xampp\\mysql\\bin\\mysql.exe", roots[r]);
        if (GetFileAttributesW(c) != INVALID_FILE_ATTRIBUTES) { wcscpy(out, roots[r]); return 1; }
    }
    return 0;
}
static void to_utf8(const WCHAR *w, char *out, int n) { if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL)) out[0] = 0; }
static void from_utf8(const char *u, WCHAR *out, int n) { if (!MultiByteToWideChar(CP_UTF8, 0, u, -1, out, n)) out[0] = 0; }

/* runs SQL through mysql.exe (tab-separated rows, no header) - returns its exit code, -1 = could not run it */
static int db_run(const char *sql, char *out, int outn)
{
    out[0] = 0;
    WCHAR root[MAX_PATH], exe[MAX_PATH], ini[MAX_PATH];
    if (!bfp_root(root)) { snprintf(out, outn, "BF-Portable's xampp\\mysql\\bin\\mysql.exe was not found - set the backend paths (Find BF-Portable)."); return -1; }
    swprintf(exe, MAX_PATH, L"%ls\\xampp\\mysql\\bin\\mysql.exe", root);
    swprintf(ini, MAX_PATH, L"%ls\\BFBC2-EMU\\config.ini", root);
    WCHAR host[64], port[16], name[64], user[64], pass[64];
    GetPrivateProfileStringW(L"database", L"db_host", L"127.0.0.1", host, 64, ini);
    GetPrivateProfileStringW(L"database", L"db_port", L"3306", port, 16, ini);
    GetPrivateProfileStringW(L"database", L"db_name", L"bf", name, 64, ini);
    GetPrivateProfileStringW(L"database", L"db_user", L"bf4", user, 64, ini);
    GetPrivateProfileStringW(L"database", L"db_pass", L"bf4", pass, 64, ini);
    WCHAR cmd[1024];
    swprintf(cmd, 1024, L"\"%ls\" -h %ls -P %ls -u \"%ls\" -N -B --default-character-set=utf8 \"%ls\"", exe, host, port, user, name);
    SetEnvironmentVariableW(L"MYSQL_PWD", pass);   /* not on the command line: no "insecure password" warning in the output */
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE inR, inW, outR, outW;
    if (!CreatePipe(&inR, &inW, &sa, 0)) return -1;
    if (!CreatePipe(&outR, &outW, &sa, 0)) { CloseHandle(inR); CloseHandle(inW); return -1; }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si; ZeroMemory(&si, sizeof si); si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    si.hStdInput = inR; si.hStdOutput = outW; si.hStdError = outW;
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    SetEnvironmentVariableW(L"MYSQL_PWD", NULL);
    CloseHandle(inR); CloseHandle(outW);
    if (!ok) { CloseHandle(inW); CloseHandle(outR); snprintf(out, outn, "Could not run mysql.exe."); return -1; }
    DWORD wr; WriteFile(inW, sql, (DWORD)strlen(sql), &wr, NULL); WriteFile(inW, "\n", 1, &wr, NULL); CloseHandle(inW);
    int len = 0; DWORD rd; char buf[2048]; DWORD t0 = GetTickCount();
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(outR, NULL, 0, NULL, &avail, NULL)) break;
        if (avail) { if (!ReadFile(outR, buf, sizeof buf, &rd, NULL) || !rd) break;
            if (len + (int)rd < outn - 1) { memcpy(out + len, buf, rd); len += rd; } continue; }
        if (WaitForSingleObject(pi.hProcess, 30) == WAIT_OBJECT_0) {
            while (PeekNamedPipe(outR, NULL, 0, NULL, &avail, NULL) && avail && ReadFile(outR, buf, sizeof buf, &rd, NULL) && rd)
                if (len + (int)rd < outn - 1) { memcpy(out + len, buf, rd); len += rd; }
            break; }
        if (GetTickCount() - t0 > 15000) { TerminateProcess(pi.hProcess, 1); break; }
    }
    out[len] = 0;
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(outR);
    return (int)code;
}
/* the first output line that is a plain number (mysql may print a warning line first) */
static int first_int(const char *out)
{
    for (const char *l = out; l && *l; ) {
        const char *e = strchr(l, '\n'); int digits = 0, other = 0;
        for (const char *c = l; *c && c != e; c++) { if (*c >= '0' && *c <= '9') digits++; else if (*c != '\r') other++; }
        if (digits && !other) return atoi(l);
        l = e ? e + 1 : NULL;
    }
    return -1;
}
static int mysql_up(void) { return process_running(L"mysqld.exe") || process_running(L"mariadbd.exe"); }

static int md5_hex(const char *s, char out[33])
{
    HCRYPTPROV prov; HCRYPTHASH h; BYTE d[16]; DWORD n = 16; int ok = 0;
    if (!CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return 0;
    if (CryptCreateHash(prov, CALG_MD5, 0, 0, &h)) {
        if (CryptHashData(h, (const BYTE *)s, (DWORD)strlen(s), 0) && CryptGetHashParam(h, HP_HASHVAL, d, &n, 0)) {
            for (int i = 0; i < 16; i++) sprintf(out + 2 * i, "%02x", d[i]); ok = 1; }
        CryptDestroyHash(h);
    }
    CryptReleaseContext(prov, 0); return ok;
}

/* ---- Players window ---- */
enum { IDP_LIST = 1100, IDP_NAME, IDP_MAIL, IDP_PW1, IDP_PW2, IDP_REG, IDP_REFRESH, IDP_DEL, IDP_CLOSE, IDP_STATUS };
static HWND g_players; static int g_mailAuto = 1; static int g_pIds[512];
static HWND padd(HWND parent, const WCHAR *cls, const WCHAR *text, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(!wcscmp(cls, L"EDIT") || !wcscmp(cls, L"LISTBOX") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE); return c;
}
static void players_status(const WCHAR *fmt, ...)
{
    WCHAR b[600]; va_list ap; va_start(ap, fmt); _vsnwprintf(b, 599, fmt, ap); va_end(ap); b[599] = 0;
    if (g_players) SetWindowTextW(GetDlgItem(g_players, IDP_STATUS), b);
}
static int need_mysql(void (*say)(const WCHAR *, ...))
{
    if (mysql_up()) return 1;
    WCHAR f[MAX_PATH]; get_text(ID_SVC, f, MAX_PATH);
    if (f[0] && !_wcsicmp(base_name(f), L"mysql_start.bat") && run(f, NULL)) say(L"MySQL was not running - starting it now. Try again in a few seconds.");
    else say(L"MySQL is not running - start it (Start All, or the XAMPP Control Panel) and try again.");
    return 0;
}
static void players_refresh(void)
{
    HWND lb = GetDlgItem(g_players, IDP_LIST); SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    if (!need_mysql(players_status)) return;
    static char out[65536];
    int r = db_run("SELECT user_id, username, email FROM a_emu_playerinfo WHERE user_id NOT BETWEEN 9001 AND 9048 ORDER BY user_id;"
                   "SELECT COUNT(*) FROM a_emu_playerinfo WHERE user_id BETWEEN 9001 AND 9048;", out, sizeof out);
    if (r != 0) { WCHAR w[600]; from_utf8(out, w, 600); players_status(L"Could not read the accounts: %ls", w[0] ? w : L"mysql.exe failed"); return; }
    int n = 0, bots = 0; char *line = out;
    while (line && *line) {
        char *e = strchr(line, '\n'); if (e) *e = 0;
        char *t1 = strchr(line, '\t');
        if (t1) { char *t2 = strchr(t1 + 1, '\t'); *t1 = 0; if (t2) *t2 = 0;
            WCHAR nm[128], em[160], row[320]; from_utf8(t1 + 1, nm, 128); from_utf8(t2 ? t2 + 1 : "", em, 160);
            swprintf(row, 320, L"%hs\t%ls\t%ls", line, nm, em);
            int idx = (int)SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)row); if (idx >= 0 && idx < 512) g_pIds[idx] = atoi(line); n++; }
        else if (line[0] >= '0' && line[0] <= '9') bots = atoi(line);
        line = e ? e + 1 : NULL;
    }
    players_status(L"%d player account(s)%ls. Bot accounts (9001-9048): %d. In BC2 you log in with the account's e-mail and password.", n, n ? L"" : L" - register one below", bots);
}
static int name_ok(const WCHAR *s) { int n = (int)wcslen(s); if (n < 3 || n > 16) return 0; for (int i = 0; i < n; i++) if (!iswalnum(s[i]) && s[i] != L'_' && s[i] != L'-' && s[i] != L'.') return 0; return 1; }
static int mail_ok(const WCHAR *s) { int n = (int)wcslen(s); const WCHAR *at = wcschr(s, L'@'); if (n < 5 || n > 60 || !at || at == s || !wcschr(at, L'.')) return 0;
    for (int i = 0; i < n; i++) if (s[i] <= L' ' || s[i] == L'\'' || s[i] == L'"' || s[i] == L'\\' || s[i] == L'<' || s[i] == L'>') return 0; return 1; }
static int pass_ok(const WCHAR *s) { int n = (int)wcslen(s); if (n < 3 || n > 32 || s[0] == L' ' || s[n - 1] == L' ') return 0;
    for (int i = 0; i < n; i++) if (s[i] < L' ' || s[i] == L'&' || s[i] == L'<' || s[i] == L'>' || s[i] == L'"' || s[i] == L'\'' || s[i] == L'\\') return 0; return 1; }
static void players_register(void)
{
    WCHAR nm[64], em[96], p1[64], p2[64];
    GetWindowTextW(GetDlgItem(g_players, IDP_NAME), nm, 64); GetWindowTextW(GetDlgItem(g_players, IDP_MAIL), em, 96);
    GetWindowTextW(GetDlgItem(g_players, IDP_PW1), p1, 64); GetWindowTextW(GetDlgItem(g_players, IDP_PW2), p2, 64);
    if (!name_ok(nm)) { players_status(L"Player name: 3-16 letters, digits, _ - or . (it is your soldier name in game)."); return; }
    if (!mail_ok(em)) { players_status(L"E-mail: something like name@bfbc2.lan - BC2 logs in with it (it does not have to be real)."); return; }
    if (!pass_ok(p1)) { players_status(L"Password: 3-32 characters, no spaces at the ends and none of  & < > \" ' \\"); return; }
    if (wcscmp(p1, p2)) { players_status(L"The two passwords are not the same."); return; }
    if (!need_mysql(players_status)) return;
    char un[64], ue[96], up[64], hash[33], sql[1024], out[4096];
    to_utf8(nm, un, 64); to_utf8(em, ue, 96); to_utf8(p1, up, 64);
    if (!md5_hex(up, hash)) { players_status(L"Could not hash the password (Windows crypto)."); return; }
    snprintf(sql, sizeof sql, "SELECT COUNT(*) FROM a_emu_playerinfo WHERE username='%s' OR email='%s';", un, ue);
    if (db_run(sql, out, sizeof out) != 0) { WCHAR w[600]; from_utf8(out, w, 600); players_status(L"Database error: %ls", w); return; }
    int have = first_int(out);
    if (have < 0) { WCHAR w[600]; from_utf8(out, w, 600); players_status(L"Unexpected answer from the database: %ls", w); return; }
    if (have > 0) { players_status(L"An account with that name or e-mail already exists."); return; }
    char code[33]; char seed[96]; snprintf(seed, sizeof seed, "%s%lu%lu", un, (unsigned long)GetTickCount(), (unsigned long)GetCurrentProcessId()); md5_hex(seed, code);
    snprintf(sql, sizeof sql,
        "INSERT INTO a_emu_playerinfo (username, password, email, localPlayer, avatar, loadstat, loadstatbf4, unlocked, register_ip, AuthCode, email_confirmed) "
        "VALUES ('%s', '%s', '%s', 1, 'default-avatar-60.png', 0, 0, 1, '', '%s%s', 1);", un, hash, ue, un, code);
    if (db_run(sql, out, sizeof out) != 0) { WCHAR w[600]; from_utf8(out, w, 600); players_status(L"Could not add the account: %ls", w); return; }
    players_refresh();
    players_status(L"Registered %ls. In Bad Company 2 log in with %ls and that password.", nm, em);
    SetWindowTextW(GetDlgItem(g_players, IDP_PW1), L""); SetWindowTextW(GetDlgItem(g_players, IDP_PW2), L"");
}
static void players_delete(void)
{
    HWND lb = GetDlgItem(g_players, IDP_LIST); int i = (int)SendMessageW(lb, LB_GETCURSEL, 0, 0);
    if (i < 0 || i >= 512) { players_status(L"Pick an account in the list first."); return; }
    WCHAR row[320]; SendMessageW(lb, LB_GETTEXT, i, (LPARAM)row);
    WCHAR q[400]; swprintf(q, 400, L"Remove this account from the backend?\n\n%ls\n\nIts stats stay in the database but nobody can log in with it any more.", row);
    if (MessageBoxW(g_players, q, L"Remove account", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    char sql[200], out[2048]; snprintf(sql, sizeof sql, "DELETE FROM a_emu_playerinfo WHERE user_id = %d AND user_id NOT BETWEEN 9001 AND 9048;", g_pIds[i]);
    if (db_run(sql, out, sizeof out) != 0) { WCHAR w[600]; from_utf8(out, w, 600); players_status(L"Could not remove it: %ls", w); return; }
    players_refresh(); players_status(L"Removed.");
}
static LRESULT CALLBACK players_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDP_NAME: if (HIWORD(w) == EN_CHANGE && g_mailAuto) { WCHAR nm[64], em[96]; GetWindowTextW(GetDlgItem(h, IDP_NAME), nm, 64);
            for (WCHAR *c = nm; *c; c++) *c = towlower(*c);
            if (nm[0]) swprintf(em, 96, L"%ls@bfbc2.lan", nm); else em[0] = 0;
            g_mailAuto = 2; SetWindowTextW(GetDlgItem(h, IDP_MAIL), em); g_mailAuto = 1; } break;
        case IDP_MAIL: if (HIWORD(w) == EN_CHANGE && g_mailAuto == 1 && GetFocus() == GetDlgItem(h, IDP_MAIL)) g_mailAuto = 0; break;
        case IDP_REG: players_register(); break;
        case IDP_REFRESH: players_refresh(); break;
        case IDP_DEL: players_delete(); break;
        case IDP_CLOSE: DestroyWindow(h); break;
        }
        return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)w, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_DESTROY: g_players = NULL; SetForegroundWindow(g_wnd); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void open_players(void)
{
    if (g_players) { SetForegroundWindow(g_players); return; }
    static int reg; if (!reg) { reg = 1; WNDCLASSW wc; ZeroMemory(&wc, sizeof wc); wc.lpfnWndProc = players_proc; wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"PhoenixPlayers"; wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); wc.hCursor = LoadCursor(NULL, IDC_ARROW); RegisterClassW(&wc); }
    RECT r = {0, 0, 520, 470}; AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    RECT pr; GetWindowRect(g_wnd, &pr);
    g_players = CreateWindowExW(0, L"PhoenixPlayers", L"Players - Project Phoenix", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                pr.left + 50, pr.top + 50, r.right - r.left, r.bottom - r.top, g_wnd, NULL, GetModuleHandleW(NULL), NULL);
    HWND H = g_players; int x = 14, y = 12;
    HWND hd = padd(H, L"STATIC", L"Accounts on this PC's backend (people who can log in to BC2 here)", 0, x, y, 490, 20, -1); SendMessageW(hd, WM_SETFONT, (WPARAM)g_bold, TRUE); y += 24;
    HWND lb = padd(H, L"LISTBOX", L"", LBS_NOTIFY | LBS_USETABSTOPS | WS_VSCROLL | WS_TABSTOP, x, y, 490, 170, IDP_LIST);
    int tabs[2] = {30, 120}; SendMessageW(lb, LB_SETTABSTOPS, 2, (LPARAM)tabs); y += 174;
    padd(H, L"BUTTON", L"Refresh", BS_PUSHBUTTON | WS_TABSTOP, x, y, 80, 26, IDP_REFRESH);
    padd(H, L"BUTTON", L"Remove selected...", BS_PUSHBUTTON | WS_TABSTOP, x + 86, y, 130, 26, IDP_DEL); y += 40;
    hd = padd(H, L"STATIC", L"Register a new player", 0, x, y, 300, 20, -1); SendMessageW(hd, WM_SETFONT, (WPARAM)g_bold, TRUE); y += 26;
    padd(H, L"STATIC", L"Player (soldier) name", 0, x, y + 3, 150, 20, -1); padd(H, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, x + 155, y, 200, 23, IDP_NAME); y += 28;
    padd(H, L"STATIC", L"E-mail (the BC2 login)", 0, x, y + 3, 150, 20, -1); padd(H, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, x + 155, y, 200, 23, IDP_MAIL); y += 28;
    padd(H, L"STATIC", L"Password", 0, x, y + 3, 150, 20, -1); padd(H, L"EDIT", L"", ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP, x + 155, y, 200, 23, IDP_PW1); y += 28;
    padd(H, L"STATIC", L"Password again", 0, x, y + 3, 150, 20, -1); padd(H, L"EDIT", L"", ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP, x + 155, y, 200, 23, IDP_PW2);
    padd(H, L"BUTTON", L"Register", BS_DEFPUSHBUTTON | WS_TABSTOP, x + 370, y - 28, 120, 51, IDP_REG); y += 36;
    padd(H, L"STATIC", L"", SS_LEFT, x, y, 400, 44, IDP_STATUS);
    padd(H, L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, x + 416, y + 6, 74, 26, IDP_CLOSE);
    g_mailAuto = 1;
    ShowWindow(H, SW_SHOW); UpdateWindow(H);
    players_refresh();
}

/* ---- LAN games window ---- */
typedef struct LanHit {
    char ip[16]; int emu, rcon, local, fromDb;
    char name[96], mode[32], map[64]; int players, maxp;
} LanHit;
#define MAX_HITS 96
enum { IDL_LIST = 1200, IDL_SCAN, IDL_JOIN, IDL_IP, IDL_CLOSE, IDL_STATUS };
static HWND g_lan; static LanHit g_hits[MAX_HITS]; static volatile LONG g_nHits, g_scanning;
static char g_myIps[8][16]; static int g_nMyIps;
#define WM_LAN_DONE (WM_APP + 16)
static void lan_status(const WCHAR *fmt, ...)
{
    WCHAR b[600]; va_list ap; va_start(ap, fmt); _vsnwprintf(b, 599, fmt, ap); va_end(ap); b[599] = 0;
    if (g_lan) SetWindowTextW(GetDlgItem(g_lan, IDL_STATUS), b);
}
static void my_ips(void)
{
    g_nMyIps = 0; char hn[256];
    if (gethostname(hn, sizeof hn)) return;
    struct addrinfo hints, *res = NULL, *a; ZeroMemory(&hints, sizeof hints); hints.ai_family = AF_INET;
    if (getaddrinfo(hn, NULL, &hints, &res)) return;
    for (a = res; a && g_nMyIps < 8; a = a->ai_next) {
        unsigned char *q = (unsigned char *)&((struct sockaddr_in *)a->ai_addr)->sin_addr;
        if (q[0] == 127 || (q[0] == 169 && q[1] == 254)) continue;
        char s[16]; snprintf(s, 16, "%u.%u.%u.%u", q[0], q[1], q[2], q[3]);
        int dup = 0; for (int i = 0; i < g_nMyIps; i++) if (!strcmp(g_myIps[i], s)) dup = 1;
        if (!dup) strcpy(g_myIps[g_nMyIps++], s);
    }
    freeaddrinfo(res);
}
static int is_my_ip(const char *ip) { if (!strncmp(ip, "127.", 4)) return 1; for (int i = 0; i < g_nMyIps; i++) if (!strcmp(g_myIps[i], ip)) return 1; return 0; }

/* many non-blocking connects at once: open[i] = 1 where ip[i]:port[i] accepted */
static void probe(const unsigned long *ip, const unsigned short *port, int n, char *open, int waitMs)
{
    SOCKET *s = calloc(n, sizeof *s); if (!s) return;
    for (int i = 0; i < n; i++) {
        open[i] = 0; s[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); if (s[i] == INVALID_SOCKET) continue;
        u_long nb = 1; ioctlsocket(s[i], FIONBIO, &nb);
        struct sockaddr_in a; ZeroMemory(&a, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(port[i]); a.sin_addr.s_addr = ip[i];
        connect(s[i], (struct sockaddr *)&a, sizeof a);
    }
    DWORD t0 = GetTickCount(); int left = n;
    while (left > 0 && GetTickCount() - t0 < (DWORD)waitMs) {
        fd_set w, e; FD_ZERO(&w); FD_ZERO(&e); int any = 0;
        for (int i = 0; i < n; i++) if (s[i] != INVALID_SOCKET && !open[i] && w.fd_count < FD_SETSIZE) { FD_SET(s[i], &w); FD_SET(s[i], &e); any = 1; }
        if (!any) break;
        struct timeval tv = {0, 50000};
        if (select(0, NULL, &w, &e, &tv) <= 0) continue;
        for (int i = 0; i < n; i++) if (s[i] != INVALID_SOCKET) {
            if (FD_ISSET(s[i], &w)) { open[i] = 1; left--; }
            else if (FD_ISSET(s[i], &e)) { closesocket(s[i]); s[i] = INVALID_SOCKET; left--; }
        }
    }
    for (int i = 0; i < n; i++) if (s[i] != INVALID_SOCKET) closesocket(s[i]);
    free(s);
}
/* BC2 RCON "serverInfo" (no login needed): OK, name, players, max, mode, map, ... */
static int rcon_info(const char *ip, int port, LanHit *h)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); if (s == INVALID_SOCKET) return 0;
    DWORD to = 1500; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to); setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&to, sizeof to);
    struct sockaddr_in a; ZeroMemory(&a, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); inet_pton(AF_INET, ip, &a.sin_addr);
    if (connect(s, (struct sockaddr *)&a, sizeof a)) { closesocket(s); return 0; }
    const char *word = "serverInfo"; uint32_t wl = (uint32_t)strlen(word);
    unsigned char pkt[64]; uint32_t hdr[3] = {1u, 12 + 4 + wl + 1, 1};
    memcpy(pkt, hdr, 12); memcpy(pkt + 12, &wl, 4); memcpy(pkt + 16, word, wl); pkt[16 + wl] = 0;
    if (send(s, (const char *)pkt, (int)hdr[1], 0) != (int)hdr[1]) { closesocket(s); return 0; }
    static unsigned char buf[16384]; int got = 0, need = 12;
    while (got < need && got < (int)sizeof buf) {
        int r = recv(s, (char *)buf + got, (int)sizeof buf - got, 0); if (r <= 0) break; got += r;
        if (got >= 12) { uint32_t sz; memcpy(&sz, buf + 4, 4); need = sz < sizeof buf ? (int)sz : (int)sizeof buf; }
    }
    closesocket(s);
    if (got < 12 || got < need) return 0;
    uint32_t nw; memcpy(&nw, buf + 8, 4); int off = 12; char words[8][96]; int k = 0;
    for (uint32_t i = 0; i < nw && k < 8 && off + 4 <= got; i++) {
        uint32_t L; memcpy(&L, buf + off, 4); off += 4; if (off + (int)L + 1 > got) break;
        int c = L < 95 ? (int)L : 95; memcpy(words[k], buf + off, c); words[k][c] = 0; k++; off += L + 1;
    }
    if (k < 6 || strcmp(words[0], "OK")) return 0;
    snprintf(h->name, sizeof h->name, "%s", words[1]); h->players = atoi(words[2]); h->maxp = atoi(words[3]);
    snprintf(h->mode, sizeof h->mode, "%s", words[4]); snprintf(h->map, sizeof h->map, "%s", words[5]);
    return 1;
}
static LanHit *hit_for(const char *ip)
{
    for (int i = 0; i < g_nHits; i++) if (!g_hits[i].fromDb && !strcmp(g_hits[i].ip, ip)) return &g_hits[i];
    if (g_nHits >= MAX_HITS) return NULL;
    LanHit *h = &g_hits[g_nHits++]; ZeroMemory(h, sizeof *h); snprintf(h->ip, 16, "%s", ip); h->local = is_my_ip(ip); return h;
}
static DWORD WINAPI lan_thread(void *arg)
{
    HWND notify = (HWND)arg;
    g_nHits = 0; my_ips();
    /* subnets to scan: a /24 round each address of this PC */
    unsigned long nets[8]; int nn = 0;
    for (int i = 0; i < g_nMyIps; i++) { unsigned long a; inet_pton(AF_INET, g_myIps[i], &a); a &= htonl(0xffffff00u);
        int dup = 0; for (int j = 0; j < nn; j++) if (nets[j] == a) dup = 1; if (!dup && nn < 8) nets[nn++] = a; }
    static const unsigned short ports[] = {18390, 47400, 47401, 47402, 47403};
    enum { NP = 5, BATCH = 760 };
    static unsigned long ip[BATCH]; static unsigned short pt[BATCH]; static char op[BATCH];
    /* this PC first (RCON listens on 127.0.0.1 only on BF-Portable) */
    { int n = 0; unsigned long lo; inet_pton(AF_INET, "127.0.0.1", &lo);
      for (int p = 0; p < NP; p++) { ip[n] = lo; pt[n] = ports[p]; n++; }
      probe(ip, pt, n, op, 600);
      const char *me = g_nMyIps ? g_myIps[0] : "127.0.0.1";
      for (int i = 0; i < n; i++) if (op[i]) { LanHit *h = hit_for(me); if (!h) break; h->local = 1;
          if (pt[i] == 18390) h->emu = 1; else if (!h->rcon && rcon_info("127.0.0.1", pt[i], h)) h->rcon = pt[i]; } }
    for (int k = 0; k < nn; k++) {
        unsigned long base = ntohl(nets[k]);
        for (int h0 = 1; h0 < 255; h0 += BATCH / NP) {
            int n = 0;
            for (int hh = h0; hh < 255 && hh < h0 + BATCH / NP; hh++) for (int p = 0; p < NP; p++) { ip[n] = htonl(base | (unsigned long)hh); pt[n] = ports[p]; n++; }
            probe(ip, pt, n, op, 900);
            for (int i = 0; i < n; i++) if (op[i]) {
                char s[16]; inet_ntop(AF_INET, &ip[i], s, 16);
                LanHit *h = hit_for(s); if (!h) continue;
                if (pt[i] == 18390) h->emu = 1;
                else if (!h->rcon && rcon_info(s, pt[i], h)) h->rcon = pt[i];
                else if (!h->rcon) h->rcon = -pt[i];   /* RCON port open, no answer */
            }
        }
    }
    /* the servers this PC's backend lists (they may run on other PCs that use this emulator) */
    if (mysql_up()) {
        static char out[32768];
        if (db_run("SELECT gnam, level, levellocation, online, pcap FROM a_bf_gameservers_bc2 WHERE client_type='rome_server' AND UNIX_TIMESTAMP() - last < 300 ORDER BY gid DESC LIMIT 20;", out, sizeof out) == 0) {
            char *line = out;
            while (line && *line && g_nHits < MAX_HITS) {
                char *e = strchr(line, '\n'); if (e) *e = 0;
                char *f[5] = {line}; int nf = 1; for (char *c = line; *c && nf < 5; c++) if (*c == '\t') { *c = 0; f[nf++] = c + 1; }
                if (nf >= 5) { LanHit *h = &g_hits[g_nHits++]; ZeroMemory(h, sizeof *h); h->fromDb = 1; h->local = 1; h->emu = 1;
                    snprintf(h->ip, 16, "%s", g_nMyIps ? g_myIps[0] : "127.0.0.1");
                    snprintf(h->name, sizeof h->name, "%s", f[0]); for (char *c = h->name; *c; c++) if (*c == '_') *c = ' ';
                    snprintf(h->map, sizeof h->map, "%s", f[1]); snprintf(h->mode, sizeof h->mode, "%s", f[2]); h->players = atoi(f[3]); h->maxp = atoi(f[4]); }
                line = e ? e + 1 : NULL;
            }
        }
    }
    InterlockedExchange(&g_scanning, 0);
    PostMessageW(notify, WM_LAN_DONE, 0, 0);
    return 0;
}
static const WCHAR *map_title(const char *level)
{
    const char *b = strrchr(level, '/'); b = b ? b + 1 : level;
    for (int i = 0; i < NMAPS; i++) { char lv[64]; snprintf(lv, sizeof lv, "%ls", kMaps[i].level); if (!_stricmp(lv, b)) return kMaps[i].name; }
    return NULL;
}
static void lan_fill(void)
{
    HWND lb = GetDlgItem(g_lan, IDL_LIST); SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    int servers = 0, emus = 0;
    for (int i = 0; i < g_nHits; i++) {
        LanHit *h = &g_hits[i]; if (!h->emu && h->rcon == 0) continue;
        WCHAR row[400], who[64], what[300];
        swprintf(who, 64, L"%hs%ls", h->ip, h->local ? L" (this PC)" : L"");
        if (h->rcon > 0 || h->fromDb) {
            const WCHAR *mt = map_title(h->map); WCHAR mw[64]; if (mt) wcscpy(mw, mt); else from_utf8(h->map, mw, 64);
            WCHAR nmw[96]; from_utf8(h->name, nmw, 96); WCHAR mdw[32]; from_utf8(h->mode, mdw, 32);
            swprintf(what, 300, L"%ls  -  %ls %ls  -  %d/%d players%ls", nmw, mdw, mw, h->players, h->maxp, h->fromDb ? L"  (listed by this PC's backend)" : L"");
            servers++;
        } else if (h->rcon < 0) { swprintf(what, 300, L"BC2 server (RCON %d did not answer)%ls", -h->rcon, h->emu ? L" + BF-Portable backend" : L""); servers++; }
        else { swprintf(what, 300, L"BF-Portable backend (BC2 emulator) - its servers show in the game's server browser"); emus++; }
        swprintf(row, 400, L"%ls\t%ls", who, what);
        int idx = (int)SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)row); SendMessageW(lb, LB_SETITEMDATA, idx, i);
    }
    WCHAR nets[200] = L"";
    for (int i = 0; i < g_nMyIps; i++) { char t[20]; snprintf(t, 20, "%s", g_myIps[i]); char *d = strrchr(t, '.'); if (d) strcpy(d + 1, "x");
        WCHAR wt[40]; swprintf(wt, 40, L"%ls%hs", nets[0] ? L", " : L"", t); if (!wcsstr(nets, wt + (nets[0] ? 2 : 0))) wcsncat(nets, wt, 199 - wcslen(nets)); }
    if (!servers && !emus) lan_status(L"Nothing found on %ls. The other PC needs BF-Portable's emulator (and a server) running, and its firewall open for TCP 18390/18395 and UDP 19567.", nets[0] ? nets : L"this PC's network");
    else lan_status(L"Found %d server(s) and %d backend(s) on %ls. Pick one and Join: the game connects to that PC's backend; join the server from the game's server browser.", servers, emus + servers, nets[0] ? nets : L"the network");
}
static void lan_scan(void)
{
    if (InterlockedCompareExchange(&g_scanning, 1, 0)) return;
    SendMessageW(GetDlgItem(g_lan, IDL_LIST), LB_RESETCONTENT, 0, 0);
    lan_status(L"Looking for BC2 backends and servers on the network (about 10 s)...");
    EnableWindow(GetDlgItem(g_lan, IDL_SCAN), FALSE);
    HANDLE t = CreateThread(NULL, 0, lan_thread, g_lan, 0, NULL); if (t) CloseHandle(t); else { g_scanning = 0; EnableWindow(GetDlgItem(g_lan, IDL_SCAN), TRUE); }
}
/* the game's bfbc2.ini (next to BFBC2Game.exe): host = the emulator the game logs in to */
static int game_ini(WCHAR *out)
{
    WCHAR g[MAX_PATH]; get_text(ID_GAME, g, MAX_PATH); if (!g[0]) return 0;
    WCHAR *sl = wcsrchr(g, L'\\'); if (!sl) return 0; *sl = 0;
    swprintf(out, MAX_PATH, L"%ls\\bfbc2.ini", g);
    return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}
static void lan_join(void)
{
    HWND lb = GetDlgItem(g_lan, IDL_LIST); int sel = (int)SendMessageW(lb, LB_GETCURSEL, 0, 0);
    WCHAR typed[64]; GetWindowTextW(GetDlgItem(g_lan, IDL_IP), typed, 64);
    char ip[16] = ""; const LanHit *h = NULL;
    if (sel >= 0) { int i = (int)SendMessageW(lb, LB_GETITEMDATA, sel, 0); if (i >= 0 && i < g_nHits) { h = &g_hits[i]; snprintf(ip, 16, "%s", h->ip); } }
    else if (typed[0]) { char t[64]; to_utf8(typed, t, 64); struct in_addr a; if (inet_pton(AF_INET, t, &a) != 1) { lan_status(L"Type an IPv4 address like 192.168.x.y (or pick a line)."); return; } snprintf(ip, 16, "%s", t); }
    else { lan_status(L"Pick a line in the list (or type the other PC's address)."); return; }
    int local = h ? h->local : is_my_ip(ip);
    WCHAR ini[MAX_PATH];
    if (!game_ini(ini)) { lan_status(L"bfbc2.ini was not found next to the game in the launcher's Game path - set the game path to BFBC2Game.exe in BF-Portable's game folder."); return; }
    backup_once(ini);
    WCHAR hw[16]; swprintf(hw, 16, L"%hs", local ? "127.0.0.1" : ip);
    WCHAR old[64]; GetPrivateProfileStringW(L"info", L"host", L"", old, 64, ini);
    WritePrivateProfileStringW(L"info", L"host", hw, ini);
    put(g_launcherIni, L"LAN", L"LastHost", hw);
    start_game();
    WCHAR nm[96] = L""; if (h && h->name[0]) from_utf8(h->name, nm, 96);
    lan_status(L"The game logs in to %ls's backend (bfbc2.ini host %ls -> %ls) and is starting. %ls%ls%lsYour account has to exist on that PC (its launcher's Players... window).",
               local ? L"this PC" : hw, old[0] ? old : L"(none)", hw, nm[0] ? L"In the server browser join \"" : L"Open the server browser and join its server. ", nm, nm[0] ? L"\". " : L"");
    status(L"Joining %ls - the game's bfbc2.ini now points at %ls.", local ? L"this PC" : hw, hw);
}
static LRESULT CALLBACK lan_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDL_SCAN: lan_scan(); break;
        case IDL_JOIN: lan_join(); break;
        case IDL_LIST: if (HIWORD(w) == LBN_DBLCLK) lan_join(); else if (HIWORD(w) == LBN_SELCHANGE) SetWindowTextW(GetDlgItem(h, IDL_IP), L""); break;
        case IDL_IP: if (HIWORD(w) == EN_CHANGE && GetFocus() == GetDlgItem(h, IDL_IP)) SendMessageW(GetDlgItem(h, IDL_LIST), LB_SETCURSEL, (WPARAM)-1, 0); break;
        case IDL_CLOSE: DestroyWindow(h); break;
        }
        return 0;
    case WM_LAN_DONE: if (g_lan) { EnableWindow(GetDlgItem(h, IDL_SCAN), TRUE); lan_fill(); } return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)w, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    case WM_DESTROY: g_lan = NULL; SetForegroundWindow(g_wnd); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void open_lan(void)
{
    if (g_lan) { SetForegroundWindow(g_lan); return; }
    static int reg; if (!reg) { reg = 1; WNDCLASSW wc; ZeroMemory(&wc, sizeof wc); wc.lpfnWndProc = lan_proc; wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"PhoenixLan"; wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); wc.hCursor = LoadCursor(NULL, IDC_ARROW); RegisterClassW(&wc); }
    RECT r = {0, 0, 640, 420}; AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    RECT pr; GetWindowRect(g_wnd, &pr);
    g_lan = CreateWindowExW(0, L"PhoenixLan", L"LAN games - Project Phoenix", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                            pr.left + 30, pr.top + 60, r.right - r.left, r.bottom - r.top, g_wnd, NULL, GetModuleHandleW(NULL), NULL);
    HWND H = g_lan; int x = 14, y = 12;
    HWND hd = padd(H, L"STATIC", L"BC2 games on your network", 0, x, y, 400, 20, -1); SendMessageW(hd, WM_SETFONT, (WPARAM)g_bold, TRUE); y += 24;
    HWND lb = padd(H, L"LISTBOX", L"", LBS_NOTIFY | LBS_USETABSTOPS | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, x, y, 610, 230, IDL_LIST);
    int tabs[1] = {110}; SendMessageW(lb, LB_SETTABSTOPS, 1, (LPARAM)tabs); SendMessageW(lb, LB_SETHORIZONTALEXTENT, 1400, 0); y += 236;
    padd(H, L"BUTTON", L"Scan the network", BS_PUSHBUTTON | WS_TABSTOP, x, y, 130, 28, IDL_SCAN);
    padd(H, L"STATIC", L"or the other PC's address:", 0, x + 145, y + 5, 160, 20, -1);
    padd(H, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, x + 305, y + 2, 130, 23, IDL_IP);
    padd(H, L"BUTTON", L"Join", BS_DEFPUSHBUTTON | WS_TABSTOP, x + 450, y, 80, 28, IDL_JOIN);
    padd(H, L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, x + 536, y, 74, 28, IDL_CLOSE); y += 38;
    padd(H, L"STATIC", L"", SS_LEFT, x, y, 610, 80, IDL_STATUS);
    ShowWindow(H, SW_SHOW); UpdateWindow(H);
    WCHAR last[64]; GetPrivateProfileStringW(L"LAN", L"LastHost", L"", last, 64, g_launcherIni);
    if (last[0] && wcscmp(last, L"127.0.0.1")) SetWindowTextW(GetDlgItem(H, IDL_IP), last);
    lan_scan();
}
