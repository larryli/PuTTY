; -*- no -*-
;
; -- Legacy Inno Setup installer script for PuTTY and its related tools.
;    Last tested with Inno Setup 5.5.9.
;    (New work should go to the MSI installer; see installer.wxs.)
;
; TODO for future releases:
;
;  - It might be nice to have an option to add PSCP, Plink and PSFTP to
;    the PATH. See wish `installer-addpath'.
;
;  - Maybe a "custom" installation might be useful? Hassle with
;    UninstallDisplayIcon, though.

[Setup]
AppName=PuTTY
AppVerName=PuTTY version 0.71
VersionInfoTextVersion=Release 0.71
AppVersion=0.71
VersionInfoVersion=0.71.0.0
AppPublisher=Simon Tatham
AppPublisherURL=https://www.chiark.greenend.org.uk/~sgtatham/putty/
AppReadmeFile={app}\README.txt
DefaultDirName={pf}\PuTTY
DefaultGroupName=PuTTY
SetupIconFile=puttyins.ico
UninstallDisplayIcon={app}\putty.exe
ChangesAssociations=yes
;ChangesEnvironment=yes -- when PATH munging is sorted (probably)
Compression=zip/9
AllowNoIcons=yes
OutputBaseFilename=installer

[Files]
; We flag all files with "restartreplace" et al primarily for the benefit
; of unattended un/installations/upgrades, when the user is running one
; of the apps at a time. Without it, the operation will fail noisily in
; this situation.
; This does mean that the user will be prompted to restart their machine
; if any of the files _were_ open during installation (or, if /VERYSILENT
; is used, the machine will be restarted automatically!). The /NORESTART
; flag avoids this.
; It might be nicer to have a "no worries, replace the file next time you
; reboot" option, but the developers have no interest in adding one.
; NB: apparently, using long (non-8.3) filenames with restartreplace is a
; bad idea. (Not that we do.)
Source: "build32\putty.exe"; DestDir: "{app}"; Flags: promptifolder replacesameversion restartreplace uninsrestartdelete
Source: "build32\pageant.exe"; DestDir: "{app}"; Flags: promptifolder replacesameversion restartreplace uninsrestartdelete
Source: "build32\puttygen.exe"; DestDir: "{app}"; Flags: promptifolder replacesameversion restartreplace uninsrestartdelete
Source: "build32\pscp.exe"; DestDir: "{app}"; Flags: promptifolder replacesameversion restartreplace uninsrestartdelete
Source: "build32\psftp.exe"; DestDir: "{app}"; Flags: promptifolder replacesameversion restartreplace uninsrestartdelete
Source: "build32\plink.exe"; DestDir: "{app}"; Flags: promptifolder replacesameversion restartreplace uninsrestartdelete
Source: "website.url"; DestDir: "{app}"; Flags: restartreplace uninsrestartdelete
Source: "..\doc\putty.chm"; DestDir: "{app}"; Flags: restartreplace uninsrestartdelete
Source: "..\doc\putty.hlp"; DestDir: "{app}"; Flags: restartreplace uninsrestartdelete
Source: "..\doc\putty.cnt"; DestDir: "{app}"; Flags: restartreplace uninsrestartdelete
Source: "..\LICENCE"; DestDir: "{app}"; Flags: restartreplace uninsrestartdelete
Source: "README.txt"; DestDir: "{app}"; Flags: isreadme restartreplace uninsrestartdelete

[Icons]
Name: "{group}\PuTTY"; Filename: "{app}\putty.exe"; AppUserModelID: "SimonTatham.PuTTY"
; We have to fall back from the .chm to the older .hlp file on some Windows
; versions.
Name: "{group}\PuTTY Manual"; Filename: "{app}\putty.chm"; MinVersion: 4.1,5.0
Name: "{group}\PuTTY Manual"; Filename: "{app}\putty.hlp"; OnlyBelowVersion: 4.1,5.0
Name: "{group}\PuTTY Web Site"; Filename: "{app}\website.url"
Name: "{group}\PSFTP"; Filename: "{app}\psftp.exe"
Name: "{group}\PuTTYgen"; Filename: "{app}\puttygen.exe"
Name: "{group}\Pageant"; Filename: "{app}\pageant.exe"
Name: "{commondesktop}\PuTTY"; Filename: "{app}\putty.exe"; Tasks: desktopicon\common; AppUserModelID: "SimonTatham.PuTTY"
Name: "{userdesktop}\PuTTY"; Filename: "{app}\putty.exe"; Tasks: desktopicon\user; AppUserModelID: "SimonTatham.PuTTY"
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

[Messages]
; Since it's possible for the user to be asked to restart their computer,
; we should override the default messages to explain exactly why, so they
; can make an informed decision. (Especially as 95% of users won't need or
; want to restart; see rant above.)
FinishedRestartLabel=One or more [name] programs are still running. Setup will not replace these program files until you restart your computer. Would you like to restart now?
; This message is popped up in a message box on a /SILENT install.
FinishedRestartMessage=One or more [name] programs are still running.%nSetup will not replace these program files until you restart your computer.%n%nWould you like to restart now?
; ...and this comes up if you try to uninstall.
UninstalledAndNeedsRestart=One or more %1 programs are still running.%nThe program files will not be removed until your computer is restarted.%n%nWould you like to restart now?
; Old versions of this installer used to prompt to remove saved settings
; and the like after the point this message was printed, so it seems
; polite to warn people that that no longer happens.
ConfirmUninstall=Are you sure you want to completely remove %1 and all of its components?%n%nNote that this will not remove any saved sessions or random seed file that %1 has created. These are harmless to leave on your system, but if you want to remove them, you should answer No here and run 'putty.exe -cleanup' before you uninstall.
