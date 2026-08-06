; Xournal++ NSIS installation script for Windows
; Author: The Xournal++ Team

;--------------------------------
; NSIS setup

Unicode true

;--------------------------------
; Includes

!include "MUI2.nsh"
!include x64.nsh
!include "${SCRIPT_DIR}\FileAssociation.nsh"
!include nsDialogs.nsh

; Options for MultiUser plugin
!define MULTIUSER_INSTALLMODE_INSTDIR "freenote"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "Software\freenote"

!define MULTIUSER_EXECUTIONLEVEL Highest ; Mixed-mode installer that can both be per-machine or per-user
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_USE_PROGRAMFILES64
!include MultiUser.nsh


;--------------------------------
; Initialization

Function .onInit
	${If} ${RunningX64}
		# 64 bit code
		SetRegView 64
	${Else}
		# 32 bit code
		MessageBox MB_OK "freenote requires 64-bit Windows. Sorry!"
		Abort
	${EndIf}

	!insertmacro MULTIUSER_INIT
FunctionEnd

Function un.onInit
	${If} ${RunningX64}
		# 64 bit code
		SetRegView 64
	${Else}
		# 32 bit code
		MessageBox MB_OK "freenote requires 64-bit Windows. Sorry!"
		Abort
	${EndIf}

    !insertmacro MULTIUSER_UNINIT
FunctionEnd

; Name and file
Name "freenote ${FREENOTE_VERSION}"
OutFile "${OUTPUT_INSTALLER_FILE}"

;--------------------------------
; Global Variables

Var StartMenuFolder

;--------------------------------
; Interface Settings

!define MUI_ABORTWARNING

;--------------------------------
; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SCRIPT_DIR}\..\LICENSE"
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE OnDirectoryLeave
!insertmacro MUI_PAGE_DIRECTORY

;Start Menu Folder Page Configuration
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "SHCTX"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\freenote"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "StartMenuEntry"
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "freenote"

!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder

!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Languages

!insertmacro MUI_LANGUAGE "English"

;-------------------------------
; Uninstall previous version

Var IsLegacyInstall
Section "" SecUninstallPrevious
	ReadRegStr $R0 SHCTX "Software\freenote" ""
	${If} $R0 == ""
		; check for legacy installation
		ReadRegStr $R0 HKCU "Software\freenote" ""
		${If} $R0 != ""
			StrCpy $IsLegacyInstall 1
		${EndIf}
		SetRegView 64
	${ENDIF}
	${If} $R0 != ""
        DetailPrint "Removing previous version located at $R0"
		ExecWait '"$R0\Uninstall.exe /S"'

		${If} $IsLegacyInstall == 1
			DetailPrint "Detected legacy installation (version 1.0.20 and below), cleaning up old files."
			RMDir /r "$R0\bin"
			RMDir /r "$R0\lib"
			RMDir /r "$R0\share"
			RMDir /r "$R0\ui"
			RMDir "$R0"

			; delete old start menu entry
			DetailPrint "Removing old start menu entries"

			!insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
			Delete "$SMPROGRAMS\$StartMenuFolder\freenote.lnk"
			Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk"
			RMDir "$SMPROGRAMS\$StartMenuFolder"
			
			DetailPrint "Removing old registry keys"
			DeleteRegKey HKLM "Software\Classes\freenote file"
			DeleteRegKey HKLM "Software\Classes\freenote Template Files"
			DeleteRegKey HKLM "Software\Classes\Xournal file"
			DeleteRegKey HKCU "Software\freenote"
		${EndIf}
    ${EndIf}
SectionEnd

;-------------------------------
; File association macros
; see https://docs.microsoft.com/en-us/windows/win32/shell/fa-file-types#registering-a-file-type

!macro SetDefaultExt EXT PROGID
	WriteRegStr SHCTX "Software\Classes\${EXT}" "" "${PROGID}"
!macroend

!macro RegisterExt EXT PROGID
	WriteRegStr SHCTX "Software\Classes\${EXT}\OpenWithProgIds" "${PROGID}" ""
	WriteRegStr SHCTX "Software\Classes\Applications\freenote.exe\SupportedTypes" "${EXT}" ""
!macroend

!macro AddProgId PROGID CMD DESC
	; Define ProgId. See https://docs.microsoft.com/en-us/windows/win32/shell/fa-progids
	WriteRegStr SHCTX "Software\Classes\${PROGID}" "" "${DESC}"
	WriteRegStr SHCTX "Software\Classes\${PROGID}\DefaultIcon" "" '"${CMD}",0'
	WriteRegStr SHCTX "Software\Classes\${PROGID}\shell" "" "open"
	WriteRegStr SHCTX "Software\Classes\${PROGID}\shell\open\command" "" '"${CMD}" "%1"'
	WriteRegStr SHCTX "Software\Classes\${PROGID}\shell\edit" "" "Edit with freenote"
	WriteRegStr SHCTX "Software\Classes\${PROGID}\shell\edit\command" "" '"${CMD}" "%1"'
!macroend

!macro DeleteProgId PROGID
	; See https://docs.microsoft.com/en-us/windows/win32/shell/fa-file-types#deleting-registry-information-during-uninstallation
	DeleteRegKey SHCTX "Software\Classes\${PROGID}"
!macroend

!define SHCNE_ASSOCCHANGED 0x08000000
!define SHCNE_CREATE 0x2
!define SHCNE_DELETE 0x4

!define SHCNF_IDLIST 0x0
!define SHCNF_PATH 0x1
!define SHCNF_FLUSH 0x1000

!macro RefreshShellIconCreate FILEPATH
	DetailPrint "Refreshing shell icon create ${FILEPATH}"
	System::Call 'shell32::SHChangeNotify(i ${SHCNE_CREATE}, i ${SHCNF_FLUSH} | ${SHCNF_PATH}, w "${FILEPATH}", i 0)'
!macroend

!macro RefreshShellIconDelete FILEPATH
	DetailPrint "Refreshing shell icon delete ${FILEPATH}"
	System::Call 'shell32::SHChangeNotify(i ${SHCNE_DELETE}, i ${SHCNF_FLUSH} | ${SHCNF_PATH}, w "${FILEPATH}", i 0)'
!macroend

;-------------------------------
; Installer Sections

Section "Associate .xopp files with freenote" SecFileXopp
	!insertmacro SetDefaultExt ".xopp" "freenote.File"
SectionEnd

Section "Associate .xopt files with freenote" SecFileXopt
	!insertmacro SetDefaultExt ".xopt" "freenote.Template"
SectionEnd

Section "Associate .xoj files with freenote" SecFileXoj
	!insertmacro SetDefaultExt ".xoj" "freenote.Xournal"
SectionEnd

Function OnDirectoryLeave
    ${IfNot} ${FileExists} "$INSTDIR\*.*"
        Return
    ${EndIf}
        
    ; Initialize Flag: 1 = Empty, 0 = Not Empty
    StrCpy $1 1 
    
    FindFirst $2 $3 "$INSTDIR\*.*"
    
	${If} $2 == ""
		Return
	${EndIf}

    ${Do}
        ${If} $3 != "."
        ${AndIf} $3 != ".."
            StrCpy $1 0
            ${Break}
        ${EndIf}
        ClearErrors
        FindNext $2 $3

        ${If} ${Errors}
            ${Break}
        ${EndIf}
    ${Loop}
    FindClose $2

    ; Check the result
    ${If} $1 == 0
        MessageBox MB_ICONEXCLAMATION|MB_YESNO|MB_DEFBUTTON2 \
        "The folder $INSTDIR is not empty.$\r$\nDo you want to continue using this folder?" \
        IDYES ignore

        Abort
        
        ignore:
    ${EndIf}
FunctionEnd

Function .onVerifyInstDir
    ; Get last component of path
    ${GetFileName} $INSTDIR $0
    
    ${If} $0 != "freenote"
        StrCpy $INSTDIR "$INSTDIR\freenote"
    ${EndIf}
FunctionEnd

Section "freenote" Secfreenote
	; Required
	SectionIn RO

	SetOutPath "$INSTDIR"

	; Files to put into the setup
	File /r ${SETUP_DIR}\*

	; Set install information
	WriteRegStr SHCTX "Software\freenote" "" '"$INSTDIR"'

	; Set program information
	WriteRegStr SHCTX "Software\Classes\Applications\freenote.exe" "" '"$INSTDIR\bin\freenote-wrapper.exe"'
	WriteRegStr SHCTX "Software\Classes\Applications\freenote.exe" "FriendlyAppName" "freenote"
	WriteRegExpandStr SHCTX "Software\Classes\Applications\freenote.exe" "DefaultIcon" '"$INSTDIR\bin\freenote-wrapper.exe",0'
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\App Paths\freenote.exe" "" '"$INSTDIR\bin\freenote-wrapper.exe"'

	; Add file type information
	!insertmacro RegisterExt ".xopp" "freenote.File"
	!insertmacro RegisterExt ".xopt" "freenote.Template"
	!insertmacro RegisterExt ".xoj" "freenote.Xournal"
	!insertmacro RegisterExt ".pdf" "freenote.AnnotatePdf"
	push $R0
	StrCpy $R0 "$INSTDIR\bin\freenote-wrapper.exe"
	!insertmacro AddProgId "freenote.File" "$R0" "freenote file"
	!insertmacro AddProgId "freenote.Template" "$R0" "freenote template file"
	!insertmacro AddProgId "freenote.Xournal" "$R0" "Xournal file"
	!insertmacro AddProgId "freenote.AnnotatePdf" "$R0" "PDF file"
	pop $R0

	; Create uninstaller
	WriteUninstaller "$INSTDIR\Uninstall.exe"
	; Add uninstall entry. See https://docs.microsoft.com/en-us/windows/win32/msi/uninstall-registry-key
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\freenote" "DisplayIcon" '"$INSTDIR\bin\freenote-wrapper.exe"'
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\freenote" "DisplayName" "freenote"
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\freenote" "DisplayVersion" "${FREENOTE_VERSION}"
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\freenote" "Publisher" "The freenote Team"
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\freenote" "URLInfoAbout" "https://github.com/readme666/freenote"
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\freenote" "InstallLocation" '"$INSTDIR"'
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\freenote" "UninstallString" '"$INSTDIR\Uninstall.exe"'
	WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\freenote" "NoModify" 1
	WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\freenote" "NoRepair" 1

	!insertmacro MUI_STARTMENU_WRITE_BEGIN Application
		;Create shortcuts
		CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
		CreateShortcut "$SMPROGRAMS\$StartMenuFolder\freenote.lnk" '"$INSTDIR\bin\freenote-wrapper.exe"'
		CreateShortcut "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" '"$INSTDIR\Uninstall.exe"'
		
		!insertmacro RefreshShellIconCreate "$SMPROGRAMS\$StartMenuFolder\freenote.lnk"
	!insertmacro MUI_STARTMENU_WRITE_END

	!insertmacro RefreshShellIcons
SectionEnd

;--------------------------------
; Descriptions

; Language strings
LangString DESC_Secfreenote ${LANG_ENGLISH} "freenote executable"
LangString DESC_SecFileXopp ${LANG_ENGLISH} "Open .xopp files with freenote"
LangString DESC_SecFileXopt ${LANG_ENGLISH} "Open .xopt files with freenote"
LangString DESC_SecFileXoj ${LANG_ENGLISH} "Open .xoj files with freenote"

; Assign language strings to sections
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${Secfreenote} $(DESC_Secfreenote)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecFileXopp} $(DESC_SecFileXopp)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecFileXopt} $(DESC_SecFileXopt)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecFileXoj} $(DESC_SecFileXoj)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
; Uninstaller

Section "Uninstall"

	SetRegView 64

	; FIXME: ask if the user wants to uninstall the user or system wide install
	ReadRegStr $0 HKCU "Software\freenote" ""
	${IF} $0 == ""
		SetShellVarContext all
	${ELSE}
		SetShellVarContext current
	${ENDIF}

	; Remove registry keys
	DeleteRegKey SHCTX "Software\freenote"
	DeleteRegKey SHCTX "Software\Classes\Applications\freenote.exe"
	DeleteRegKey SHCTX "Software\Microsoft\Windows\CurrentVersion\App Paths\freenote.exe"
	DeleteRegKey SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\freenote"

	!insertmacro DeleteProgId "freenote.File"
	!insertmacro DeleteProgId "freenote.Template"
	!insertmacro DeleteProgId "freenote.Xournal"
	!insertmacro DeleteProgId "freenote.AnnotatePdf"

	; Clean up start menu
	!insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
	Delete "$SMPROGRAMS\$StartMenuFolder\freenote.lnk"
	Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk"
	RMDir "$SMPROGRAMS\$StartMenuFolder"

	; Remove files
	RMDir /r "$INSTDIR\bin"
	RMDir /r "$INSTDIR\lib"
	RMDir /r "$INSTDIR\share"
	RMDir /r "$INSTDIR\ui"
	Delete "$INSTDIR\Uninstall.exe"
	RMDir "$INSTDIR"

	!insertmacro RefreshShellIconDelete "$SMPROGRAMS\$StartMenuFolder\freenote.lnk"
	!insertmacro RefreshShellIcons
SectionEnd
