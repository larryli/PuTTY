; -*- no -*-
; putty.iss
;
; -- Inno Setup installer script for PuTTY and its related tools.
;
; TODO for future releases:
;
;  - It might be nice to have an option to add PSCP, Plink and PSFTP to
;    the PATH. This is probably only practical on NT-class systems; I
;    believe doing this on 9x would require mucking around with
;    AUTOEXEC.BAT.
;
;  - Maybe a "custom" installation might be useful? Hassle with icons,
;    though.

[Setup]
AppName=PuTTY
AppVerName=PuTTY version 0.57
VersionInfoTextVersion=Release 0.57
AppVersion=0.57
;FIXME -- enable this when we've got it going for individual EXEs too
;         and are committed to the version numbering scheme.
;VersionInfoVersion=0.57.0.0
AppPublisher=Simon Tatham
AppPublisherURL=http://www.chiark.greenend.org.uk/~sgtatham/putty/
AppReadmeFile={app}\README.txt
DefaultDirName={pf}\PuTTY
DefaultGroupName=PuTTY
UninstallDisplayIcon={app}\putty.exe
ChangesAssociations=yes
;ChangesEnvironment=yes -- when PATH munging is sorted (probably)
Compression=zip/9
AllowNoIcons=yes

[Files]
Source: "putty.exe"; DestDir: "{app}"
Source: "pageant.exe"; DestDir: "{app}"
Source: "puttygen.exe"; DestDir: "{app}"
Source: "pscp.exe"; DestDir: "{app}"
Source: "psftp.exe"; DestDir: "{app}"
Source: "plink.exe"; DestDir: "{app}"
Source: "website.url"; DestDir: "{app}"
Source: "..\doc\putty.hlp"; DestDir: "{app}"
Source: "..\doc\putty.cnt"; DestDir: "{app}"
Source: "..\LICENCE"; DestDir: "{app}"
Source: "..\README.txt"; DestDir: "{app}"; Flags: isreadme

[Icons]
Name: "{group}\PuTTY"; Filename: "{app}\putty.exe"; Comment: "SSH, Telnet and Rlogin client";
Name: "{group}\PuTTY Manual"; Filename: "{app}\putty.hlp"
Name: "{group}\PuTTY Web Site"; Filename: "{app}\website.url"
Name: "{group}\PSFTP"; Filename: "{app}\psftp.exe"; Comment: "Command-line interactive SFTP client"
Name: "{group}\PuTTYgen"; Filename: "{app}\puttygen.exe"; Comment: "PuTTY SSH key generation utility"
Name: "{group}\Pageant"; Filename: "{app}\pageant.exe"; Comment: "PuTTY SSH authentication agent"
Name: "{commondesktop}\PuTTY"; Filename: "{app}\putty.exe"; Tasks: desktopicon\common
Name: "{userdesktop}\PuTTY"; Filename: "{app}\putty.exe"; Tasks: desktopicon\user
; Putting this in {commonappdata} doesn't seem to work, on 98SE at least.
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\PuTTY"; Filename: "{app}\putty.exe"; Tasks: quicklaunchicon

[Tasks]
Name: desktopicon; Description: "Create a &desktop icon for PuTTY"; GroupDescription: "Additional icons:"; Flags: unchecked
Name: desktopicon\common; Description: "For all users"; GroupDescription: "Additional icons:"; Flags: exclusive unchecked
Name: desktopicon\user; Description: "For the current user only"; GroupDescription: "Additional icons:"; Flags: exclusive unchecked
Name: quicklaunchicon; Description: "Create a &Quick Launch icon for PuTTY (current user only)"; GroupDescription: "Additional icons:"; Flags: unchecked
Name: associate; Description: "&Associate .PPK files (PuTTY Private Key) with Pageant and PuTTYgen"; GroupDescription: "Other tasks:"

[Registry]
Root: HKCR; Subkey: ".ppk"; ValueType: string; ValueName: ""; ValueData: "PuTTYPrivateKey"; Flags: uninsdeletevalue; Tasks: associate
Root: HKCR; Subkey: "PuTTYPrivateKey"; ValueType: string; ValueName: ""; ValueData: "PuTTY Private Key File"; Flags: uninsdeletekey; Tasks: associate
Root: HKCR; Subkey: "PuTTYPrivateKey\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\pageant.exe,0"; Tasks: associate
Root: HKCR; Subkey: "PuTTYPrivateKey\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\pageant.exe"" ""%1"""; Tasks: associate
Root: HKCR; Subkey: "PuTTYPrivateKey\shell\edit"; ValueType: string; ValueName: ""; ValueData: "&Edit"; Tasks: associate
Root: HKCR; Subkey: "PuTTYPrivateKey\shell\edit\command"; ValueType: string; ValueName: ""; ValueData: """{app}\puttygen.exe"" ""%1"""; Tasks: associate
; Add to PATH on NT-class OS?

[UninstallRun]
; -cleanup-during-uninstall is an undocumented option that tailors the
; message displayed.
Filename: "{app}\putty.exe"; Parameters: "-cleanup-during-uninstall"; RunOnceId: "PuTTYCleanup"; StatusMsg: "Cleaning up saved sessions etc (optional)..."
